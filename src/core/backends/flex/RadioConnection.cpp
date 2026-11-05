#include "RadioConnection.h"
#include "FlexPttWireSession.h"
#include "LanBindAddress.h"
#include "core/LogManager.h"

#include <algorithm>
#include <QEventLoop>
#include <QNetworkProxy>
#include <QTimer>

#ifdef Q_OS_LINUX
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#elif defined(Q_OS_MACOS)
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#elif defined(Q_OS_WIN)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
// MinGW headers may lack TCP_INFO_v0 / SIO_TCP_INFO (Windows 10 1703+)
#ifndef SIO_TCP_INFO
#define SIO_TCP_INFO _WSAIORW(IOC_VENDOR, 39)
typedef struct _TCP_INFO_v0 {
    ULONG    State;  // TCPSTATE enum — not used, just padding
    ULONG    Mss;
    ULONG64  ConnectionTimeMs;
    BOOLEAN  TimestampsEnabled;
    ULONG    RttUs;
    ULONG    MinRttUs;
    ULONG    BytesInFlight;
    ULONG    Cwnd;
    ULONG    SndWnd;
    ULONG    RcvWnd;
    ULONG    RcvBuf;
    ULONG64  BytesOut;
    ULONG64  BytesIn;
    ULONG    BytesReordered;
    ULONG    BytesRetrans;
    ULONG    FastRetrans;
    ULONG    DupAcksIn;
    ULONG    TimeoutEpisodes;
    UCHAR    SynRetrans;
} TCP_INFO_v0;
#endif
#endif

namespace AetherSDR {

static constexpr int HEARTBEAT_INTERVAL_MS = 30000;

// Cap the line-assembly buffer.  A buggy or hostile peer that dribbles bytes
// without ever sending '\n' would otherwise grow m_readBuffer unbounded until
// QByteArray refuses to allocate (process OOM).  CAT lines are tens of bytes;
// 16 MiB is wildly larger than any legitimate radio command or status burst.
// Mirrors the pattern from GHSA-7w4w-wfqm-wh93 (M2, RigctlServer) with a
// looser cap appropriate to the chattier status stream.
static constexpr int kMaxReadBuffer = 16 * 1024 * 1024;

static QString formatHexId(quint32 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

RadioConnection::RadioConnection(QObject* parent)
    : QObject(parent)
{
}

RadioConnection::~RadioConnection()
{
    disconnectFromRadio();
}

void RadioConnection::init()
{
    m_independentPtt = std::make_unique<FlexPttWireSession>(
        [this](quint32 seq, const QString& command) { return writeSocketCommand(seq, command); },
        [this](const TxStopEvidence& evidence) { emit independentPttStopped(evidence); });
    m_independentPttTimer = new QTimer(this);
    m_independentPttTimer->setInterval(25);
    connect(m_independentPttTimer, &QTimer::timeout, this, [this] {
        m_independentPtt->poll(TxCoordinator::monotonicMs());
        updateIndependentPttTimer();
    });
    resetSocket();
    m_heartbeat = new QTimer(this);

    m_heartbeat->setInterval(HEARTBEAT_INTERVAL_MS);
    connect(m_heartbeat, &QTimer::timeout, this, &RadioConnection::onHeartbeat);
}

void RadioConnection::resetSocket()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
    }
    m_socket = new QTcpSocket(this);
    // LAN radio uses a proprietary CAT/VITA stream on port 4992; an OS or
    // application HTTP/SOCKS proxy can never tunnel it sensibly.  SmartSDR
    // dodges this because .NET's TcpClient ignores WinHTTP proxy settings,
    // while Qt's QTcpSocket honours QNetworkProxy::applicationProxy() by
    // default.  Force NoProxy so a system proxy doesn't block the connect.
    // (WAN/SmartLink paths intentionally keep default proxy handling.) (#3389)
    m_socket->setProxy(QNetworkProxy::NoProxy);

    connect(m_socket, &QTcpSocket::connected,
            this, &RadioConnection::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &RadioConnection::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &RadioConnection::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &RadioConnection::onSocketError);
}

void RadioConnection::connectToRadio(const RadioInfo& info)
{
    connectToHost(info.address, info.port,
                  info.bindSettings.mode,
                  info.bindSettings.bindAddress,
                  info.sessionBindAddress);
}

void RadioConnection::connectToHost(const QHostAddress& address,
                                    quint16 port,
                                    RadioBindMode bindMode,
                                    const QHostAddress& explicitBindAddr,
                                    const QHostAddress& sessionBindAddr)
{
    if (m_state.load() != ConnectionState::Disconnected) return;
    if (!m_socket) { qCWarning(lcConnection) << "RadioConnection: init() not called"; return; }

    m_bindMode = bindMode;
    m_explicitBindAddr = explicitBindAddr;
    m_sessionBindAddr = sessionBindAddr;
    m_radioAddr = address;
    m_localAddr = QHostAddress();
    m_localPort = 0;
    m_socket->abort();
    // Qt 6.11/6.12 can retain a bound transport after a refused connect while
    // reporting UnconnectedState. abort()/close() leave that engine intact;
    // discard it before the next bind or connect instead of reusing its error.
    if (m_socket->socketDescriptor() != -1) {
        resetSocket();
    }
    resetSessionState();

    const QHostAddress preferredBindAddr =
        (bindMode == RadioBindMode::Explicit) ? explicitBindAddr : sessionBindAddr;
    const bool shouldBind = matchesLanAddressFamily(address, preferredBindAddr);
    if (bindMode == RadioBindMode::Explicit && !preferredBindAddr.isNull() && !shouldBind) {
        const QString msg = QStringLiteral("Local TCP source address %1 has a different address family from radio %2")
                                .arg(preferredBindAddr.toString(), address.toString());
        qCWarning(lcConnection) << "RadioConnection:" << msg;
        setState(ConnectionState::Error);
        emit errorOccurred(msg);
        // The selected source remains mandatory, but the attempt can be retried
        // once it is corrected -- so leave the session Disconnected, not wedged
        // in Error (connectToHost early-returns unless state is Disconnected).
        setState(ConnectionState::Disconnected);
        return;
    }

    if (shouldBind) {
        if (!m_socket->bind(preferredBindAddr, 0)) {
            const QString msg = QStringLiteral("Failed to bind local TCP source address %1: %2")
                                    .arg(preferredBindAddr.toString(), m_socket->errorString());
            if (bindMode == RadioBindMode::Explicit) {
                qCWarning(lcConnection) << "RadioConnection:" << msg;
                m_socket->abort();
                m_socket->close();
                setState(ConnectionState::Error);
                emit errorOccurred(msg);
                // The selected source remains mandatory, but the attempt can be retried.
                setState(ConnectionState::Disconnected);
                return;
            }

            qCDebug(lcConnection) << "RadioConnection: auto bind to"
                                  << preferredBindAddr.toString()
                                  << "failed, falling back to OS routing:"
                                  << m_socket->errorString();
        } else {
            qCDebug(lcConnection) << "RadioConnection: bound local TCP source to"
                                  << preferredBindAddr.toString()
                                  << ":" << m_socket->localPort()
                                  << "(mode"
                                  << (bindMode == RadioBindMode::Explicit ? "Explicit" : "Auto")
                                  << ")";
        }
    } else {
        qCDebug(lcConnection) << "RadioConnection: no explicit local TCP bind, letting OS route"
                              << "(mode"
                              << (bindMode == RadioBindMode::Explicit ? "Explicit" : "Auto")
                              << ")";
    }

    qCDebug(lcConnection) << "RadioConnection: connecting to" << address.toString() << ":" << port;
    setState(ConnectionState::Connecting);
    m_socket->connectToHost(address, port);
}

void RadioConnection::resetSessionState()
{
    m_pttProtocol.store(PttProtocol::Unknown);
    if (m_independentPtt) {
        m_independentPtt->disconnect();
        updateIndependentPttTimer();
    }
    // Partial lines and ping replies belong to exactly one TCP session.
    //
    // FlexLib does this explicitly too, and for the same reason: its
    // TcpCommandCommunication keeps the line-assembly buffer as a member of a
    // long-lived object and clears it under _tcpReadSyncObj in Disconnect()
    // (reference/FlexLib_API_v4.1.5.39794/FlexLib/TcpCommandCommunication.cs:249).
    // It is not per-session by lifetime. We also reset at the connect edge,
    // which covers a hard kill that never reaches a disconnect path. (#5649)
    m_readBuffer.clear();
    m_handle = 0;
    m_lastPingSeq = 0;
    m_pingStopwatch.invalidate();
    // Every session edge invalidates work queued by the previous session.
    ++m_sessionGeneration;
}

void RadioConnection::disconnectFromRadio()
{
    // Reset twice, deliberately: here, and again at the end, because
    // waitForDisconnected(2000) pumps the event loop and can deliver readyRead,
    // refilling m_readBuffer and m_handle after this first pass. Neither call
    // is redundant; do not collapse them. (#5653 review)
    resetSessionState();
    if (m_heartbeat) m_heartbeat->stop();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        writeDisconnectMarker();
        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
            m_socket->waitForDisconnected(2000);
    }
    resetSessionState();
}

void RadioConnection::gracefulDisconnect(quint32 handle,
                                         const QString& rxStreamId,
                                         quint32 streamRemoveSeq)
{
    if (!m_socket || m_socket->state() == QAbstractSocket::UnconnectedState) {
        disconnectFromRadio();
        return;
    }

    if (m_heartbeat) m_heartbeat->stop();

    QElapsedTimer totalTimer;
    totalTimer.start();
    bool streamRemoveAcked = rxStreamId.isEmpty();
    qint64 streamRemoveMs = -1;

    // The radio needs to process teardown commands before TCP close begins.
    // Fire-and-forget C0 writes can leave the Flex session table stale (#2218).
    if (!rxStreamId.isEmpty() && streamRemoveSeq != 0) {
        QElapsedTimer stepTimer;
        stepTimer.start();
        streamRemoveAcked =
            sendCommandAndWait(streamRemoveSeq,
                               QString("stream remove 0x%1").arg(rxStreamId),
                               2000);
        streamRemoveMs = stepTimer.elapsed();
    }

    QElapsedTimer closeTimer;
    closeTimer.start();
    disconnectFromRadio();
    const qint64 closeMs = closeTimer.elapsed();

    qCInfo(lcConnection).nospace()
        << "RadioConnection: disconnect summary "
        << "handle=" << formatHexId(handle)
        << " streamId=" << (rxStreamId.isEmpty() ? QStringLiteral("<none>") : rxStreamId)
        << " streamSeq=" << streamRemoveSeq
        << " streamAck=" << (streamRemoveAcked ? "yes" : "no")
        << " streamMs=" << streamRemoveMs
        << " closeMs=" << closeMs
        << " totalMs=" << totalTimer.elapsed();
}

void RadioConnection::writeCommand(quint32 seq, const QString& command)
{
    if (m_commandSinkForTest) {
        m_commandSinkForTest(seq, command);
        return;
    }
    if (m_independentPtt) {
        m_independentPtt->otherCommand(command, TxCoordinator::monotonicMs());
    }
    (void)writeSocketCommand(seq, command);
}

bool RadioConnection::independentPttSupported() const
{
    return isConnected()
        && m_pttProtocol.load() == PttProtocol::Supported;
}

bool RadioConnection::independentPttReady() const
{
    return independentPttSupported() && m_independentPtt && m_independentPtt->ready();
}

void RadioConnection::writeIndependentPtt(quint32 seq, const TxCoordinator::Command& command)
{
    if (independentPttSupported() && m_independentPtt) {
        m_independentPtt->key(seq, command, TxCoordinator::monotonicMs());
        updateIndependentPttTimer();
    } else {
        command.completion.finish();
    }
}

void RadioConnection::stopIndependentPtt(quint32 seq, const TxCoordinator::Operation& operation,
                                         const TxCoordinator::StopRequest& request)
{
    if (isConnected() && m_independentPtt) {
        m_independentPtt->stop(seq, operation, request, TxCoordinator::monotonicMs());
        updateIndependentPttTimer();
    }
}

void RadioConnection::updateIndependentPttTimer()
{
    if (!m_independentPttTimer || !m_independentPtt) {
        return;
    }
    if (!m_independentPtt->needsPolling()) {
        m_independentPttTimer->stop();
    } else if (!m_independentPttTimer->isActive()) {
        m_independentPttTimer->start();
    }
}

bool RadioConnection::writeSocketCommand(quint32 seq, const QString& command)
{
    if (!isConnected() || !m_socket) {
        return false;
    }

    const QByteArray data = CommandParser::buildCommand(seq, command);
    if (command.startsWith("ping")) {
        m_lastPingSeq = seq;
        m_pingStopwatch.restart();  // fallback timer in case TCP_INFO unavailable
    } else {
        qCDebug(lcConnection) << "TX:" << data.trimmed();
    }
    const bool written = m_socket->write(data) == data.size();
    m_socket->flush();   // force immediate kernel send for keepalive reliability
    return written;
}

void RadioConnection::onSocketConnected()
{
    qCDebug(lcConnection) << "RadioConnection: TCP connected";
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_radioAddr = m_socket->peerAddress();
    m_localAddr = m_socket->localAddress();
    m_localPort = m_socket->localPort();
    qCDebug(lcConnection) << "RadioConnection: local TCP endpoint"
                          << m_localAddr.toString() << ":" << m_localPort;
    qDebug() << "RTT method:" << (kernelRttMs() >= 0 ? "kernel TCP_INFO" : "QElapsedTimer fallback");
}

void RadioConnection::onSocketDisconnected()
{
    resetSessionState();
    qCDebug(lcConnection) << "RadioConnection: TCP disconnected";
    if (m_heartbeat) m_heartbeat->stop();
    m_localAddr = QHostAddress();
    m_localPort = 0;
    setState(ConnectionState::Disconnected);
    emit disconnected();
}

void RadioConnection::onSocketError(QAbstractSocket::SocketError)
{
    if (!m_socket) return;
    const QString msg = m_socket->errorString();
    qCWarning(lcConnection) << "RadioConnection: socket error:" << msg;
    setState(ConnectionState::Error);
    emit errorOccurred(msg);
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        resetSessionState();
        setState(ConnectionState::Disconnected);
    }
}

void RadioConnection::onReadyRead()
{
    if (!m_socket) return;
    m_readBuffer.append(m_socket->readAll());
    if (m_readBuffer.size() > kMaxReadBuffer) {
        qCWarning(lcConnection) << "RadioConnection: read buffer exceeded"
                                << kMaxReadBuffer << "bytes without newline — disconnecting";
        m_socket->disconnectFromHost();
        m_readBuffer.clear();
        return;
    }
    int newlinePos;
    while ((newlinePos = m_readBuffer.indexOf('\n')) >= 0) {
        // Strip the line terminator only. The evidence path must see malformed
        // whitespace; CommandParser separately trims for legacy presentation.
        QString line = QString::fromUtf8(m_readBuffer.left(newlinePos));
        if (line.endsWith(u'\r')) {
            line.chop(1);
        }
        m_readBuffer.remove(0, newlinePos + 1);
        if (!QStringView(line).trimmed().isEmpty()) {
            processLine(line);
        }
    }
}

void RadioConnection::onHeartbeat()
{
}

void RadioConnection::processLine(const QString& line)
{
    if (m_independentPtt) {
        m_independentPtt->observe(line, TxCoordinator::monotonicMs());
        updateIndependentPttTimer();
    }
    // GPS coordinates are never useful in a support log. Drop the raw status
    // at the source; AsyncLogWriter also scrubs coordinate-shaped fields as a
    // defense against future logging paths.
    const bool isGps = line.contains("|gps ");
    bool isPingReply = false;
    if (m_lastPingSeq && line.startsWith("R")) {
        isPingReply = line.startsWith(QString("R%1|").arg(m_lastPingSeq));
        if (isPingReply) {
            int rtt = kernelRttMs();
            if (rtt < 0)
                rtt = static_cast<int>(m_pingStopwatch.elapsed());  // fallback
            emit pingRttMeasured(rtt);
            m_lastPingSeq = 0;
        }
    }
    if (!isGps && !isPingReply) {
        qCDebug(lcConnection) << "RX:" << line;
    }

    ParsedMessage msg = CommandParser::parseLine(line);
    emit messageReceived(msg);

    switch (msg.type) {
    case MessageType::Version: {
        // Inspect the raw prologue, not the tolerant presentation parser's
        // trimmed value. A duplicate/late V cannot renegotiate TX authority.
        const bool supported = m_pttProtocol.load() == PttProtocol::Unknown
            && !isConnected() && clientHandle() == 0 && line.startsWith(u'V')
            && FlexPttWireSession::supportsProtocol(QStringView(line).mid(1));
        m_pttProtocol.store(supported ? PttProtocol::Supported : PttProtocol::Rejected);
        if (!supported && m_independentPtt) {
            m_independentPtt->rejectProtocol();
        }
        emit versionReceived(msg.object);
        break;
    }
    case MessageType::Handle: {
        const bool firstHandle = clientHandle() == 0;
        m_handle = msg.handle;
        if (m_independentPtt) {
            // The evidence parser must not accept the presentation parser's
            // default-zero or partially parsed identity.
            bool ok = false;
            const quint32 handle = line.mid(1).toUInt(&ok, 16);
            const QStringView digits = QStringView(line).mid(1);
            const bool hexOnly = digits.size() == 8 && std::all_of(digits.begin(), digits.end(), [](QChar ch) {
                return (ch >= u'0' && ch <= u'9') || (ch >= u'a' && ch <= u'f')
                    || (ch >= u'A' && ch <= u'F');
            });
            if (m_pttProtocol.load() == PttProtocol::Supported && firstHandle
                && ok && line.size() == 9 && line.startsWith(u'H')
                && hexOnly && handle != 0 && clientHandle() == handle) {
                m_independentPtt->reset(m_sessionGeneration, handle);
            } else {
                m_pttProtocol.store(PttProtocol::Rejected);
                m_independentPtt->rejectProtocol();
            }
        }
        qCDebug(lcConnection) << "RadioConnection: assigned handle" << QString::number(m_handle, 16);
        setState(ConnectionState::Connected);
        if (m_heartbeat) m_heartbeat->start();
        emit connected();
        break;
    }
    case MessageType::Response:
        emit commandResponse(msg.sequence, msg.resultCode, msg.object);
        break;
    case MessageType::Status:
        emit statusReceived(msg.object, msg.kvs);
        break;
    default:
        break;
    }
}

// ─── Kernel-level TCP RTT ────────────────────────────────────────────────────
// Read the smoothed RTT maintained by the kernel's TCP congestion control.
// This is measured from TCP ACK round-trips at the kernel level, completely
// independent of Qt event loop buffering or application-layer timing.

int RadioConnection::kernelRttMs() const
{
    if (!m_socket) return -1;
    const auto fd = m_socket->socketDescriptor();
    if (fd == -1) return -1;

#ifdef Q_OS_LINUX
    struct tcp_info info{};
    socklen_t len = sizeof(info);
    if (getsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_INFO, &info, &len) == 0)
        return static_cast<int>(info.tcpi_rtt / 1000);  // µs → ms
#elif defined(Q_OS_MACOS)
    struct tcp_connection_info info{};
    socklen_t len = sizeof(info);
    if (getsockopt(static_cast<int>(fd), IPPROTO_TCP, TCP_CONNECTION_INFO, &info, &len) == 0)
        return static_cast<int>(info.tcpi_srtt);  // already ms on macOS
#elif defined(Q_OS_WIN)
    TCP_INFO_v0 info{};
    DWORD infoLen = sizeof(info);
    DWORD version = 0;
    if (WSAIoctl(static_cast<SOCKET>(fd), SIO_TCP_INFO, &version, sizeof(version),
                 &info, sizeof(info), &infoLen, nullptr, nullptr) == 0)
        return static_cast<int>(info.RttUs / 1000);  // µs → ms
#endif

    return -1;  // unsupported platform or call failed
}

void RadioConnection::setState(ConnectionState s)
{
    m_state.store(s);
    emit stateChanged(s);
}

bool RadioConnection::sendCommandAndWait(quint32 seq, const QString& command, int timeoutMs)
{
    if (seq == 0 || !m_socket || m_socket->state() == QAbstractSocket::UnconnectedState)
        return false;

    bool received = false;
    int responseCode = 0;
    QString responseBody;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    const QMetaObject::Connection responseConn =
        connect(this, &RadioConnection::commandResponse, &loop,
                [&](quint32 responseSeq, int code, const QString& body) {
            if (responseSeq != seq)
                return;
            received = true;
            responseCode = code;
            responseBody = body;
            loop.quit();
        });

    const QMetaObject::Connection disconnectedConn =
        connect(m_socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);

    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    writeCommand(seq, command);
    timer.start(timeoutMs);
    loop.exec();

    disconnect(responseConn);
    disconnect(disconnectedConn);

    if (!received) {
        qCWarning(lcConnection) << "RadioConnection: timed out waiting for teardown response"
                                << "seq" << seq << "command" << command;
        return false;
    }

    qCDebug(lcConnection) << "RadioConnection: teardown response"
                          << "seq" << seq
                          << "code" << Qt::hex << responseCode
                          << "body" << responseBody;
    return true;
}

void RadioConnection::writeDisconnectMarker()
{
    if (!m_socket || m_socket->state() == QAbstractSocket::UnconnectedState)
        return;

    m_socket->write(QByteArray(1, '\x04'));
    m_socket->flush();
    m_socket->waitForBytesWritten(250);
}

} // namespace AetherSDR
