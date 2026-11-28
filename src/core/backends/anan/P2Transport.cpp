#include "core/backends/anan/P2Transport.h"
#include "core/backends/anan/AnanNativeUdp.h"
#include "core/backends/anan/P1Protocol.h"
#include "core/backends/anan/AnanRoute.h"
#include "core/backends/anan/AnanCodecFeed.h"
#include "core/AppSettings.h"
#include "core/backends/anan/AnanUiState.h"   // build 751

#include <QStringList>
#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
#include <QVariant>

#include <algorithm>
#include <limits>
#include <map>
#include <chrono>
#include <cmath>
#include <numbers>
#include <cstring>

#ifdef Q_OS_WIN
// windows.h's min/max macros break std::min/std::max in this file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>   // timeBeginPeriod (winmm, linked by aethercore)
#endif

Q_LOGGING_CATEGORY(lcAnanP2T, "aether.anan.p2t")

namespace AetherSDR::anan {

namespace {

// Build 221: which local address the session socket binds to, per radio.
// Starts alternate between the wildcard (the routing table picks the adapter)
// and the adapter the radio's discovery reply came in on (AnanRoute.h), until
// one gets I/Q back; that one is then kept for the radio.
std::mutex g_bindMutex;
std::map<quint32, int> g_silentStarts;      // starts that got nothing at all back
std::map<quint32, quint32> g_workingBind;   // 0 = wildcard

template <std::size_t N>
qint64 sendDatagram(QUdpSocket& s, const std::array<std::uint8_t, N>& buf,
                    const QHostAddress& host, quint16 port, quint64& txBytes,
                    quint64& txPackets)
{
    const qint64 sent = s.writeDatagram(reinterpret_cast<const char*>(buf.data()),
                                        static_cast<qint64>(N), host, port);
    if (sent > 0) {
        txBytes += static_cast<quint64>(sent);
        ++txPackets;
    }
    return sent;
}

// Monotonic milliseconds (a wall-clock step must not start or stop the
// host-clock stand-in).
qint64 steadyMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::span<const std::uint8_t> asBytes(const QByteArray& b)
{
    return {reinterpret_cast<const std::uint8_t*>(b.constData()),
            static_cast<std::size_t>(b.size())};
}

}  // namespace

P2Transport::P2Transport(QObject* parent) : hl2::MetisClient(parent)
{
    m_keepalive = new QTimer(this);
    m_keepalive->setInterval(kKeepaliveMs);
    connect(m_keepalive, &QTimer::timeout, this, &P2Transport::onKeepaliveTick);

    m_boardWait = new QTimer(this);
    m_boardWait->setSingleShot(true);
    connect(m_boardWait, &QTimer::timeout, this, &P2Transport::onBoardWaitTimeout);

    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, [this] {
        if (!m_up) {
            // ANAN-100D port (06.11): what the radio did send meanwhile --
            // nothing at all, or a session (mic / status) without DDC I/Q.
            qCWarning(lcAnanP2T).nospace()
                << "ANAN P2 transport: no I/Q within " << kConnectTimeoutMs << " ms; from the radio: "
                << m_counters.rxPackets << " packets, mic " << (m_diagMicPkts - m_connMicPkts0)
                << ", status " << m_statusPackets << ", Discovery reply "
                << (m_configured ? "yes" : "no");
            emit connectFailed(QStringLiteral("no IQ stream from the radio within %1 ms of start")
                                   .arg(kConnectTimeoutMs) + noteBindSilent());
        }
    });

    m_watchdog = new QTimer(this);
    m_watchdog->setInterval(kWatchdogTickMs);
    connect(m_watchdog, &QTimer::timeout, this, &P2Transport::onWatchdogTick);

    m_spkTimer = new QTimer(this);
    m_spkTimer->setInterval(2);
    m_spkTimer->setTimerType(Qt::PreciseTimer);
    connect(m_spkTimer, &QTimer::timeout, this, &P2Transport::onSpeakerPacerTick);

    m_txTimer = new QTimer(this);
    m_txTimer->setInterval(2);   // host stand-in only (onTxPacerTick)
    // Build 231: the power / SWR meters at 25 updates a second while keyed.
    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(40);
    connect(m_meterTimer, &QTimer::timeout, this, [this] {
        if (m_mox && m_up)
            publishTelemetry();
    });
    m_txTimer->setTimerType(Qt::PreciseTimer);
    connect(m_txTimer, &QTimer::timeout, this, &P2Transport::onTxPacerTick);

    m_paddleHangTimer = new QTimer(this);
    m_paddleHangTimer->setSingleShot(true);
    m_paddleHangTimer->setTimerType(Qt::PreciseTimer);
    connect(m_paddleHangTimer, &QTimer::timeout, this, [this] {
        m_paddleHold = false;
        updateRadioPtt();
    });
    m_hostPaddleHangTimer = new QTimer(this);   // build 851
    m_hostPaddleHangTimer->setSingleShot(true);
    m_hostPaddleHangTimer->setTimerType(Qt::PreciseTimer);
    connect(m_hostPaddleHangTimer, &QTimer::timeout, this, [this] {
        m_hostPaddleHold = false;
        updateRadioPtt();
    });

    // The speaker sender: its own thread, time critical (not a QObject child:
    // it is joined in the destructor).
    m_spkThread = QThread::create([this] { speakerSenderLoop(); });
    m_spkThread->setObjectName(QStringLiteral("anan-spk"));
    m_spkThread->start(QThread::TimeCriticalPriority);
}

P2Transport::~P2Transport()
{
    stop();
    if (m_spkThread) {
        {
            std::lock_guard<std::mutex> lock(m_spkMutex);
            m_spkQuit = true;
        }
        m_spkCv.notify_all();
        m_spkThread->wait();
        delete m_spkThread;
        m_spkThread = nullptr;
    }
}

int P2Transport::attenuationForLnaGainDb(int gainDb) noexcept
{
    return std::clamp(-gainDb, 0, kMaxStepAttenuationDb);
}

int P2Transport::receiversForBoard(const BoardProfile& profile, int numDdc) noexcept
{
    // Build 466 (operator, 16.11): on a PureSignal board every DDC is a
    // receiver -- RX3 / RX4 wrap onto DDC0 / DDC1, which PureSignal takes back
    // while it transmits (hwDdcFor(), psHoldsLowDdcs()).
    // Only when the DDCs above the base are fewer than the receivers this
    // backend drives: a board with room above keeps them all there.
    if (profile.psSupported && profile.rxDdcBase >= 2 && numDdc > profile.rxDdcBase
        && numDdc - profile.rxDdcBase < kMaxDdcs)
        return std::clamp(numDdc, 1, kMaxDdcs);
    const int room = numDdc > 0 ? numDdc - profile.rxDdcBase : kMaxDdcs;
    return std::clamp(room, 1, kMaxDdcs);
}

int P2Transport::ddcWrap() const noexcept
{
    if (m_p1 || !m_profile.psSupported || m_profile.rxDdcBase < 2
        || m_discoveredNumDdc <= m_profile.rxDdcBase
        || m_discoveredNumDdc - m_profile.rxDdcBase >= kMaxDdcs)
        return 0;   // no PureSignal DDCs to share, or room enough above them
    return m_discoveredNumDdc;
}

int P2Transport::hwDdcOfRx(int rx) const noexcept
{
    return static_cast<int>(hwDdcFor(static_cast<std::size_t>(rx),
                                     static_cast<std::size_t>(m_profile.rxDdcBase),
                                     static_cast<std::size_t>(ddcWrap())));
}

bool P2Transport::psHoldsLowDdcs() const noexcept
{
    if (!m_psEnabled)
        return false;
    if (m_moxAtomic.load(std::memory_order_relaxed))
        return true;
    return TxCoordinator::monotonicMs() < m_psReleaseUntilMs.load(std::memory_order_relaxed);
}

void P2Transport::updateHeldMask() noexcept
{
    std::uint32_t mask = 0;
    if (m_p1) {
        // Build 468: Protocol 1 -- the receivers on the PureSignal pair.
        // (Not on two DDCs -- Hermes II: PureSignal borrows RX1 / RX2's own
        // DDCs there, which then carry it, as before build 468.)
        if (m_profile.psSupported && m_p1Plan.psFbDdc >= 0 && m_p1Plan.nddc > 2
            && psHoldsLowDdcs()) {
            for (int i = 0; i < m_numRx; ++i) {
                const int d = m_p1Plan.rxDdc[static_cast<std::size_t>(i)];
                if (d == m_p1Plan.psFbDdc || d == m_p1Plan.psFbDdc + 1)
                    mask |= 1u << i;
            }
        }
        // Build 469: receivers past the DDCs the radio really streams.
        for (int i = m_p1Plan.maxRx; i < m_numRx; ++i)
            mask |= 1u << i;
    } else if (ddcWrap() > 0 && psHoldsLowDdcs()) {
        for (int i = 0; i < m_numRx; ++i) {
            if (hwDdcOfRx(i) < 2)
                mask |= 1u << i;
        }
    }
    m_heldMask.store(mask, std::memory_order_relaxed);
}

int P2Transport::rateKsps() const noexcept
{
    return hl2::sampleRateHz(m_p.sampleRate) / 1000;
}

// ---- discovery -------------------------------------------------------------

QList<hl2::MetisClient::Discovered> P2Transport::discover(int timeoutMs,
                                                          const QHostAddress& target,
                                                          quint16 port)
{
    QList<Discovered> out;
    QUdpSocket s;
    if (!s.bind(QHostAddress::AnyIPv4, 0))
        return out;
    const auto pkt = buildDiscovery();
    s.writeDatagram(reinterpret_cast<const char*>(pkt.data()),
                    static_cast<qint64>(pkt.size()), target,
                    port ? port : kRadioPort);
    // Build 194: the same boards with Protocol 1 firmware answer only this.
    const auto pkt1 = hl2::discoveryRequest();
    s.writeDatagram(reinterpret_cast<const char*>(pkt1.data()),
                    static_cast<qint64>(pkt1.size()), target,
                    port ? port : kRadioPort);

    QDeadlineTimer deadline(timeoutMs);
    QSet<QString> seen;
    // Build 242 (operator, 10.11: the radio left running in receive, streaming
    // I/Q to a session that is gone; every connect then hung the program at
    // "HL2 hardware options"): a busy radio can send its stream to this socket
    // (thousands of datagrams a second). Two things made that a hang on the
    // I/O thread -- and so on the GUI thread, which waits for this call with
    // BlockingQueuedConnection:
    //  - the inner loop drained "while datagrams are pending" and never looked
    //    at the deadline, so a socket that never runs dry never returned;
    //  - every datagram, discovery reply or not, went through localRouteFor(),
    //    which enumerates the PC's network interfaces (milliseconds on
    //    Windows), slower than the stream arrives.
    // The deadline now bounds the whole call, and only a parsed discovery
    // reply is looked up.
    auto noteRoute = [](const QNetworkDatagram& dg) {
        // Build 221: the adapter this reply came in on (AnanRoute.h).
        if (const LocalRoute route = localRouteFor(dg); route.valid())
            noteHostRoute(dg.senderAddress().toIPv4Address(), route.local.toIPv4Address());
    };
    while (!deadline.hasExpired()) {
        if (!s.waitForReadyRead(static_cast<int>(std::max<qint64>(1, deadline.remainingTime()))))
            break;
        while (s.hasPendingDatagrams() && !deadline.hasExpired()) {
            const QNetworkDatagram dg = s.receiveDatagram();
            const QByteArray data = dg.data();
            if (const auto r1 = hl2::parseDiscoveryReply(asBytes(data))) {
                // Protocol 1 firmware (EF FE 02/03 ...): its board type mapped
                // to the Protocol 2 id the profiles and models are keyed by.
                const auto id = p2BoardIdForP1(r1->boardId, r1->gatewareVersion);
                if (!id)
                    continue;
                const QString key = dg.senderAddress().toString();
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                noteRoute(dg);
                noteProtocol1Host(dg.senderAddress().toIPv4Address(),
                                  Protocol1HostInfo{*id, r1->gatewareVersion});
                Discovered d;
                d.reply.mac = r1->mac;
                d.reply.gatewareVersion = r1->gatewareVersion;
                d.reply.boardId = *id;
                d.reply.streaming = r1->streaming;
                d.reply.numRx = static_cast<std::uint8_t>(p1DdcPlanFor(*id).maxRx);   // build 468
                // Build 723: a Hermes-Lite 2 reports its receivers (discovery
                // 0x13); not more than the plan's.
                if (*id == kBoardIdHermesLite && r1->numRx > 0)
                    d.reply.numRx = std::min<std::uint8_t>(d.reply.numRx, r1->numRx);
                d.address = dg.senderAddress();
                out.append(d);
                continue;
            }
            const auto reply = parseDiscoveryReply(asBytes(data));
            if (!reply || !reply->isSupportedAnan())
                continue;
            const QString key = dg.senderAddress().toString();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            noteRoute(dg);
            noteProtocol1Host(dg.senderAddress().toIPv4Address(), false);
            const BoardProfile profile =
                boardProfileFor(reply->boardId).value_or(defaultBoardProfile());
            Discovered d;
            d.reply.mac = reply->mac;
            d.reply.gatewareVersion = reply->firmwareVer;
            d.reply.boardId = reply->boardId;
            d.reply.streaming = reply->streaming;
            d.reply.numRx = static_cast<std::uint8_t>(receiversForBoard(profile, reply->numDdc));
            d.address = dg.senderAddress();
            // localAddress left empty: the session socket binds the wildcard.
            out.append(d);
        }
    }
    return out;
}

// ---- session ---------------------------------------------------------------

// Build 221: the session socket's bind got I/Q -- keep it for this radio.
void P2Transport::noteBindWorked()
{
    const quint32 hostIp = m_p.host.toIPv4Address();
    std::lock_guard<std::mutex> lock(g_bindMutex);
    g_silentStarts.erase(hostIp);
    g_workingBind[hostIp] = m_bindAddr == QHostAddress(QHostAddress::AnyIPv4) ? 0u : m_bindAddr.toIPv4Address();
}

// Build 221: a start without I/Q. When nothing at all came back, the next
// start tries the other bind, and the operator is told what to look at.
// Returns the text appended to the connection error.
QString P2Transport::noteBindSilent()
{
    if (m_counters.rxPackets != 0)
        return {};
    const quint32 hostIp = m_p.host.toIPv4Address();
    {
        std::lock_guard<std::mutex> lock(g_bindMutex);
        ++g_silentStarts[hostIp];
        g_workingBind.erase(hostIp);
    }
    const LocalRoute found = routeForLocalAddress(m_learnedBind);
    QString why;
    if (found.valid() && !sameSubnet(found.local, m_p.host, found.prefix)) {
        why = QStringLiteral("; the radio %1 was found on adapter \"%2\" (%3/%4), which is not the radio's "
                             "subnet -- put the adapter and the radio in one subnet")
                  .arg(m_p.host.toString(), found.adapter, found.local.toString())
                  .arg(found.prefix);
    } else if (found.valid()) {
        const bool onAdapter = m_bindAddr != QHostAddress(QHostAddress::AnyIPv4);
        why = QStringLiteral("; nothing came back from %1 (sent %2). The radio answers the discovery "
                             "broadcast, so look for a VPN, a firewall rule or another program "
                             "holding the radio%3")
                  .arg(m_p.host.toString(),
                       onAdapter ? QStringLiteral("from adapter \"%1\"").arg(found.adapter)
                                 : QStringLiteral("by the routing table"),
                       onAdapter ? QString()
                                 : QStringLiteral("; the next attempt sends from adapter \"%1\"")
                                       .arg(found.adapter));
    } else {
        why = QStringLiteral("; nothing came back from %1 -- check the address, a VPN or a firewall rule")
                  .arg(m_p.host.toString());
    }
    qCWarning(lcAnanP2T).noquote() << "ANAN transport:" << why.mid(2);
    return why;
}

bool P2Transport::start(const hl2::MetisClient::Params& params)
{
    // Not stop(): a PureSignal push queued ahead of this start is the new
    // session's and stays.
    stopSession(false);
    m_p = params;
    m_numRx = effectiveNumRx(params);
    m_numRx = std::clamp(m_numRx, 1, kMaxDdcs);
    m_rxHz.fill(params.rxFrequencyHz);
    m_dither = params.ditherBit;
    m_random = params.randomBit;
    m_profile = defaultBoardProfile();
    m_configured = false;
    m_up = false;
    m_discoveredNumDdc = 0;
    m_p1 = false;
    m_p1Pending.reset();
    m_p1Seq = 0;
    m_p1CcIdx = 0;
    m_p1AudioAcc = 0;
    m_p1StartTries = 0;
    m_p1DiagSeen = m_p1DiagBadSize = m_p1DiagNoSync = m_p1DiagOneFrame = 0;
    m_p1RateLogged = false;
    m_p1Firmware.reset();
    m_p1WbOrder = 0;
    m_p1WbClock.invalidate();
    m_p1Mic.clear();
    m_p1MicPhase = 0;
    m_expectedSeq.fill(std::nullopt);
    for (auto& q : m_pending)
        q.clear();
    m_counters = {};
    m_micSeqValid = false;
    m_fpgaBuf = FpgaBuffers{};   // the radio resets its counters when we stop it
    m_bufReported = false;
    m_bufTxEmpty = m_bufTxOvf = m_bufSpkEmpty = m_bufSpkOvf = 0;
    m_linkReported = false;
    {
        std::lock_guard<std::mutex> lock(m_statusSnapMutex);
        m_statusSnapValid = false;
    }
    m_phyClkDev = INT_MIN;
    m_ref10M = kRefNone;
    m_ref10MState = -2;
    // A new session has had no I/Q yet (the connect timeout reports how
    // long it waited).
    m_sinceIq.invalidate();
    m_connMicPkts0 = m_diagMicPkts;
    m_eeeAdv = -1;
    m_gbStatus = -1;
    for (auto& v : m_link)
        v = 0;
    m_dropTotal = 0;
    m_warnedPorts.clear();
    m_warnedTx = false;
    m_diagClock.invalidate();
    m_diagSamples = m_diagGaps = m_diagLost = 0;
    m_adc0Overload = m_overloadInWindow = m_radioPtt = false;
    m_ovlLogOn = false;
    m_pttLine = m_paddleDown = m_paddleHold = false;
    m_paddleHangTimer->stop();
    m_telemetryClock.invalidate();
    m_statusPackets = 0;
    m_diagStatusSeen = 0;

    // Wildcard bind first, as P2Client (verified on the ANAN-100D): binding to
    // a stale Params::localAddress once left the session socket deaf on a
    // multi-interface PC. Build 221 (operator, 09.11: make the routing
    // reliable): when a wildcard start got nothing back, the next start binds
    // to the adapter the radio was found on (Thetis binds to it), and so on in
    // turn; what worked is kept for this radio.
    {
        const quint32 hostIp = m_p.host.toIPv4Address();
        QHostAddress learned;
        bool chosen = false;   // an adapter picked in Connect by IP -> Advanced
        const auto swept = hostRoute(hostIp);
        if (m_p.localAddress.protocol() == QAbstractSocket::IPv4Protocol) {
            learned = m_p.localAddress;
            chosen = !swept || *swept != learned.toIPv4Address();
        } else if (swept) {
            learned = QHostAddress(*swept);
        }
        if (!learned.isNull() && !routeForLocalAddress(learned).valid())
            learned = QHostAddress();   // no longer an address of this PC
        m_learnedBind = learned;
        m_bindAddr = QHostAddress(QHostAddress::AnyIPv4);
        int silent = 0;
        {
            std::lock_guard<std::mutex> lock(g_bindMutex);
            silent = g_silentStarts.count(hostIp) ? g_silentStarts[hostIp] : 0;
            const auto w = g_workingBind.find(hostIp);
            if (w != g_workingBind.end()) {
                if (w->second != 0)
                    m_bindAddr = QHostAddress(w->second);
            } else if (!learned.isNull()) {
                // Build 240: when the radio is outside the adapter's subnet the
                // routing table cannot reach it, so the adapter (broadcast)
                // goes first there.
                const LocalRoute lr = routeForLocalAddress(learned);
                const bool offSubnet = lr.valid() && !sameSubnet(lr.local, m_p.host, lr.prefix);
                if ((silent % 2 == 1) != (chosen || offSubnet))
                    m_bindAddr = learned;
            }
        }
        if (m_bindAddr.isNull() || (m_bindAddr != QHostAddress(QHostAddress::AnyIPv4)
                                    && !routeForLocalAddress(m_bindAddr).valid()))
            m_bindAddr = QHostAddress(QHostAddress::AnyIPv4);
        // Build 240 (log 10.11, radio 192.168.1.168 static, PC adapter
        // 169.254.56.125/16): the radio answers the discovery BROADCAST on that
        // adapter, but unicast to it cannot leave there (Windows: "Unable to
        // send a message") and the routing table sends it elsewhere -- 0 packets
        // back on every start. On such an adapter the commands go out as
        // broadcasts from it, as the discovery does; the radio replies to our
        // address, which it can reach.
        m_txHost = m_p.host;
        {
            const LocalRoute lr = routeForLocalAddress(learned);
            const bool offSubnet = !learned.isNull() && lr.valid()
                                   && !sameSubnet(lr.local, m_p.host, lr.prefix);
            if (offSubnet && m_bindAddr == learned) {
                m_txHost = QHostAddress(QHostAddress::Broadcast);
                qCWarning(lcAnanP2T).noquote()
                    << "ANAN transport: the radio" << m_p.host.toString() << "is not in the subnet of adapter"
                    << QStringLiteral("\"%1\" (%2/%3)").arg(lr.adapter, lr.local.toString()).arg(lr.prefix)
                    << "-- sending to it as broadcasts from that adapter. Better: give the adapter an "
                       "address in the radio's subnet.";
            }
        }
        const LocalRoute via = routeForLocalAddress(m_bindAddr);
        qCInfo(lcAnanP2T).noquote().nospace()
            << "ANAN transport: session socket on "
            << (via.valid() ? via.local.toString() + QStringLiteral(" (adapter \"") + via.adapter + QStringLiteral("\")")
                            : QStringLiteral("any address (routing table)"))
            << " to " << m_p.host.toString()
            << (m_txHost == QHostAddress(QHostAddress::Broadcast) ? QStringLiteral(" (as broadcasts)") : QString())
            << (silent > 0 ? QStringLiteral(", after %1 start(s) with nothing back").arg(silent) : QString());
    }
    m_sock = new QUdpSocket(this);
    if (!m_sock->bind(m_bindAddr, 0)) {
        m_sock->deleteLater();
        m_sock = nullptr;
        return false;
    }
    connect(m_sock, &QUdpSocket::readyRead, this, &P2Transport::onReadyRead);
    // ANAN-100D port (operator, 31.10: "packets to the radio should leave at
    // once"): UDP has no send buffering of its own (no Nagle; writeDatagram
    // hands the datagram to the stack, which sends it at once). What can hold
    // a packet back is our own pacing -- the 1 ms TX/speaker timers ran on
    // Windows' default 15.6 ms timer tick -- so the system timer runs at 1 ms
    // for the session, and the datagrams are marked DSCP EF (expedited) for
    // any switch or NIC queue that honours it.
#ifdef Q_OS_WIN
    if (!m_timerPeriodSet && timeBeginPeriod(1) == TIMERR_NOERROR)
        m_timerPeriodSet = true;
#endif
    m_sock->setSocketOption(QAbstractSocket::TypeOfServiceOption, 0xB8);   // DSCP 46 (EF)
    // ANAN-100D port (log 02.11 19:03:15: 25 DDC + 22 mic packets lost in
    // one go -- the I/O thread late by ~30 ms, and Windows' default 64 KB
    // receive buffer holds only ~30 ms of this stream): 8 MB holds seconds.
    m_sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 8 * 1024 * 1024);
    m_sock->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 1024 * 1024);
    qCInfo(lcAnanP2T) << "ANAN P2 transport: socket buffers rx"
                      << m_sock->socketOption(QAbstractSocket::ReceiveBufferSizeSocketOption).toInt() << "tx"
                      << m_sock->socketOption(QAbstractSocket::SendBufferSizeSocketOption).toInt() << "bytes";
    {
        std::unique_lock<std::shared_mutex> guard(m_sendGuard);
        m_nativeFd = m_sock->socketDescriptor();
        m_nativeIp = m_txHost.toIPv4Address();
    }
    m_run = true;

    // Discovery first, from this socket: the radio streams to the port it
    // came from. General / DDC-Specific / High Priority follow once the reply
    // names the board (configureSession), or after kBoardWaitMs.
    const qint64 sent = sendDatagram(*m_sock, buildDiscovery(), m_txHost, kRadioPort,
                                     m_counters.txBytes, m_counters.txPackets);
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: Discovery to " << m_txHost.toString()
                                << ":" << kRadioPort << " from local port " << m_sock->localPort()
                                << (sent > 0 ? "" : " -- SEND FAILED: ") << (sent > 0 ? QString() : m_sock->errorString());
    // Build 194: and the Protocol 1 one. A radio answers one of them; a
    // Protocol 2 reply wins (onBoardWaitTimeout / handleDatagram).
    sendDatagram(*m_sock, hl2::discoveryRequest(), m_txHost, kRadioPort,
                 m_counters.txBytes, m_counters.txPackets);
    m_boardWait->start(kBoardWaitMs);
    m_connectTimer->start(kConnectTimeoutMs);
    m_linkClock.start();
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: starting " << m_numRx
                                << " receiver(s) at " << rateKsps() << " ksps, "
                                << m_rxHz[0] << " Hz, ATT ADC1 " << m_adcAtt[0] << " dB, ADC2 " << m_adcAtt[1] << " dB";
    return true;
}

void P2Transport::configureSession(const BoardProfile& profile, bool fromReply)
{
    if (!m_run || !m_sock || m_configured)
        return;
    m_boardWait->stop();
    m_reportedBoardId.store(profile.boardId, std::memory_order_relaxed);
    m_profile = forcedProfileOr(profile);   // build 267
    m_configured = true;
    m_sessionBoardId.store(m_profile.boardId, std::memory_order_relaxed);   // build 423
    // Build 213: over Protocol 1 every board this backend drives has the
    // Thetis PureSignal pair (Hermes DDC2/DDC3, Hermes II DDC0/DDC1 while
    // transmitting, the others DDC3/DDC4), single-ADC boards included.
    if (m_p1)
        m_profile.psSupported = true;
    if (!m_profile.psSupported && m_psEnabled) {
        std::lock_guard<std::mutex> lock(m_txMutex);
        m_psEnabled = false;
        qCWarning(lcAnanP2T) << "ANAN: PureSignal is not available on the" << m_profile.modelName << "-- off";
    }
    m_p1Atomic.store(m_p1, std::memory_order_relaxed);
    if (m_p1) {
        // Protocol 1 (build 194). The Odyssey status is a Protocol 2
        // feature here; the DDC plan is Thetis'. PureSignal since build 213.
        m_profile.odysseyStatus = false;
        // Protocol 1 reports ANAN-10E / 100B as HERMES; the operator tells
        // them apart (Thetis does the same) -- since build 267 by forcedly
        // assigning the 10E / 100B, which gives the HERMES II profile here
        // (AnanForcedModel.h migrates the earlier model choice to that).
        m_p1Plan = p1DdcPlanFor(m_profile.boardId);
        m_p1BoardNddc = m_p1Plan.nddc;
        m_p1RateChecked = false;   // build 469: p1CheckDdcCount()
        m_p1RateCheckFromMs = 0;
        m_p1RateCheckPkts = 0;
        m_numRx = std::clamp(m_numRx, 1, m_p1Plan.maxRx);   // build 468: up to 4
        m_p1Ddc.assign(static_cast<std::size_t>(m_p1Plan.nddc), {});
        qCInfo(lcAnanP2T).nospace()
            << "ANAN P1 transport: " << m_profile.modelName << " (Protocol 1 firmware, gateware "
            << int(m_p1Gateware) << "), " << m_p1Plan.nddc << " DDC, RX1 on DDC" << m_p1Plan.rxDdc[0]
            << (m_numRx > 1 ? QStringLiteral(", RX2 on DDC%1").arg(m_p1Plan.rxDdc[1]) : QString())
            << QStringLiteral(" (build 468: up to %1 receivers, RX3 / RX4 on DDC%2 / DDC%3)")
                   .arg(m_p1Plan.maxRx).arg(m_p1Plan.rxDdc[2]).arg(m_p1Plan.rxDdc[3])
            << ", " << hl2::sampleRateHz(static_cast<hl2::SampleRate>(p1RateCode())) / 1000 << " ksps"
            << "";
        {
            std::lock_guard<std::mutex> lock(m_spkMutex);
            m_spkQueue.clear();
        }
        m_spkCv.notify_one();   // the speaker thread stands aside (it waits on m_p1Atomic)
        sendP1Start();
        m_keepalive->start();
        m_meterTimer->start();   // build 231
        m_watchdog->start();
        return;
    }
    if (m_discoveredNumDdc > 0)
        m_numRx = std::min(m_numRx, receiversForBoard(m_profile, m_discoveredNumDdc));
    qCInfo(lcAnanP2T).nospace()
        << "ANAN P2 transport: " << m_profile.modelName << " (board " << int(m_profile.boardId)
        << (fromReply ? ", from Discovery" : ", assumed") << "), receivers on DDC"
        << m_profile.rxDdcBase << ".." << (m_profile.rxDdcBase + m_numRx - 1)
        << (ddcWrap() > 0 ? QStringLiteral(" (build 466: RX3 / RX4 on DDC0 / DDC1 when running, "
                                           "PureSignal's while it transmits; board has %1 DDC)")
                                .arg(m_discoveredNumDdc)
                          : QString());
    // Thetis' SendStart() (ChannelMaster network.c): General (1024), DDC
    // Specific (1025), Transmitter Specific (1026), High Priority with run = 1
    // (1027) -- the same four, in that order, at every start.
    sendGeneral();
    sendDdcSpecific();
    sendTransmitSpecific();
    sendHighPriority(true);
    m_keepalive->start();
    m_meterTimer->start();   // build 231
    m_watchdog->start();
    {
        std::lock_guard<std::mutex> lock(m_spkMutex);
        m_spkQueue.clear();
    }
    m_sessionUp.store(true, std::memory_order_release);
    m_spkPrimed = false;
    m_spkLeadSent = false;
    m_spkPacketsSent = 0;
    m_spkClock.start();
}

void P2Transport::onBoardWaitTimeout()
{
    if (!m_run || m_configured)
        return;
    if (m_p1Pending) {
        m_p1 = true;
        configureSession(*m_p1Pending, true);
        return;
    }
    // Build 212: no reply to this socket's own (unicast) requests, but the
    // discovery sweep found a Protocol 1 radio at this address. Some Protocol 1
    // firmware answers discovery only when it is broadcast; it still streams
    // to whoever sends it the start command, so start Protocol 1, as Thetis
    // does. (Before, this fell through to Protocol 2, which such a radio
    // ignores: "no IQ ... 0 packets".)
    if (const auto known = protocol1HostInfo(m_p.host.toIPv4Address())) {
        if (const auto profile = boardProfileFor(known->boardId)) {
            qCInfo(lcAnanP2T).nospace()
                << "ANAN transport: no Discovery reply to this socket within " << kBoardWaitMs
                << " ms; the discovery sweep found a Protocol 1 " << profile->modelName
                << " (gateware " << int(known->gateware) << ") at this address -- starting Protocol 1";
            m_p1 = true;
            m_p1Gateware = known->gateware;
            configureSession(*profile, false);
            return;
        }
    }
    qCWarning(lcAnanP2T) << "ANAN P2 transport: no Discovery reply within" << kBoardWaitMs
                         << "ms; assuming" << defaultBoardProfile().modelName;
    configureSession(defaultBoardProfile(), false);
}

void P2Transport::stop()
{
    stopSession(true);
}

void P2Transport::stopSession(bool toPowerOnState)
{
#ifdef Q_OS_WIN
    if (m_timerPeriodSet) {
        timeEndPeriod(1);
        m_timerPeriodSet = false;
    }
#endif
    if (m_keepalive)
        m_keepalive->stop();
        m_meterTimer->stop();
    if (m_boardWait)
        m_boardWait->stop();
    if (m_connectTimer)
        m_connectTimer->stop();
    if (m_watchdog)
        m_watchdog->stop();
    if (m_txTimer)
        m_txTimer->stop();
    if (m_spkTimer)
        m_spkTimer->stop();
    m_sessionUp.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_spkMutex);
        m_spkQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_txMutex);
        m_mox = false;
        m_moxAtomic.store(false, std::memory_order_relaxed);
        m_txQueue.clear();
        m_unkeyTail = false;   // build 632
        ++m_unkeyTailGen;
    }
    if (m_sock) {
        if (m_configured && m_p1) {
            // Thetis SendStopToMetis: EF FE 04 00 (sent twice, it is UDP).
            for (int i = 0; i < 2; ++i)
                sendDatagram(*m_sock, hl2::metisCommand(0x00), m_txHost, kRadioPort,
                             m_counters.txBytes, m_counters.txPackets);
        } else if (m_configured) {
            // Thetis' SendStop(): High Priority with run = 0, nothing else.
            m_cwxBits.store(0, std::memory_order_relaxed);   // build 850
            m_hostPaddleDown = m_hostPaddleHold = false;    // build 851
            m_hostPaddleHangTimer->stop();
            sendHighPriority(false);
        }
        {
            // No native send may be in flight on this descriptor once it closes.
            std::unique_lock<std::shared_mutex> guard(m_sendGuard);
            m_nativeFd = -1;
        }
        m_sock->close();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    const bool wasUp = m_up;
    m_run = false;
    m_up = false;
    m_configured = false;
    // The next session starts as the first one in the program does: with
    // PureSignal off. PureSignal switches itself on again once that session
    // runs (AnanPureSignal::setTxChannel on the new TXA generation, the same
    // push as at the first start).
    if (toPowerOnState && m_psEnabled) {
        std::lock_guard<std::mutex> lock(m_txMutex);
        m_psEnabled = false;
    }
    if (wasUp)
        emit linkDown();
}

void P2Transport::sendGeneral()
{
    if (m_p1)
        return;   // Protocol 1: the C&C rotation carries the state
    const auto pkt = buildGeneral(m_profile.alexEnableMask, paEffective(), m_wb);
    sendDatagram(*m_sock, pkt, m_txHost, kRadioPort, m_counters.txBytes, m_counters.txPackets);
}

void P2Transport::sendDdcSpecific()
{
    if (m_p1)
        return;
    std::vector<DdcConfig> ddcs;
    // A single-ADC board (Hermes, G2E) has only ADC0 to give.
    const int lastAdc = std::max(0, m_profile.numAdcs - 1);
    for (int i = 0; i < m_numRx; ++i)
        ddcs.push_back(DdcConfig{rateKsps(), std::min(m_rxAdc[static_cast<std::size_t>(i)], lastAdc)});
    auto pkt = buildDdcSpecific(ddcs, m_profile.numAdcs, false, false, m_profile.rxDdcBase,
                                      PsDdcOptions{m_psEnabled && m_mox,
                                                   m_psLoopback ? m_profile.numAdcs : m_psAdc},
                                      ddcWrap());   // build 466
    // Build 439: bytes 5 / 6 per ADC (bit N = ADC N; Thetis network.c), from
    // the ANT menus; only the ADCs this board has.
    const int adcBits = (1 << std::clamp(m_profile.numAdcs, 1, 3)) - 1;
    pkt[5] = static_cast<std::uint8_t>(m_ditherMask & adcBits);
    pkt[6] = static_cast<std::uint8_t>(m_randomMask & adcBits);
    sendDatagram(*m_sock, pkt, m_txHost, kDdcSpecificPort, m_counters.txBytes, m_counters.txPackets);
}

void P2Transport::sendHighPriority(bool run)
{
    if (!m_sock || m_p1)
        return;
    std::array<std::uint32_t, kMaxDdcs> words{};
    for (int i = 0; i < m_numRx; ++i)
        words[static_cast<std::size_t>(i)] = phaseWord(static_cast<double>(m_rxHz[static_cast<std::size_t>(i)]));

    HighPriorityOptions o;
    o.hwDdcBase = m_profile.rxDdcBase;
    o.alexScheme = m_profile.alexScheme;
    o.rxLpfFrequencyHz = static_cast<double>(m_rxHz[0]);
    o.boardHasSpeakerAmpMute = m_profile.hasSpeakerAmpMute;
    o.boardHasAdc1FilterBank = m_profile.hasAdc1FilterBank;
    o.mirrorRxWordsBelowBase = m_profile.rxDdcBase >= 2;
    o.hwDdcWrap = ddcWrap();   // build 466: RX3 / RX4 on DDC0 / DDC1
    // Transmit state (DUC0, PTT0, drive, Alex TX LPF / relay).
    o.transmitting = run && m_mox;
    o.txFrequencyHz = static_cast<double>(m_txHz ? m_txHz : m_rxHz[0]);
    o.ducPhaseWord = phaseWord(o.txFrequencyHz);
    o.driveLevel = cwDemoActive() ? 0 : m_drive;   // CW demo: no RF
    o.paEnabled = paEffective();
    o.rxAntenna = m_rxAnt;
    o.tunerStart = m_tunerStart;
    o.xvtrEnable = m_xvtrTx;   // build 637
    o.alexPreamp = m_lna;
    o.txAntenna = m_txAnt;
    o.psEnabled = m_psEnabled;
    o.psFeedbackSource = m_psSource;
    o.psCouplerOnTx = m_psCouplerOnTx;   // build 502
    o.cwxBits = run ? m_cwxBits.load(std::memory_order_relaxed) : std::uint8_t{0};   // build 850
    // The ADC0 (Ant1/2/3 path) filters follow the first receiver listening
    // to ADC0 -- receiver 0 unless it was moved to ADC2; a stopping radio is
    // handed back bypassed.
    std::size_t filterRx = 0;
    for (int i = 0; i < m_numRx; ++i) {
        if (effectiveAdc(i) == 0) {
            filterRx = static_cast<std::size_t>(i);
            break;
        }
    }
    o.rxLpfFrequencyHz = static_cast<double>(m_rxHz[filterRx]);
    // Build 279: the banks per ADC, bypassed when their receivers span bands.
    const BankChoice bank0 = bankFor(0);
    const BankChoice bank1 = bankFor(1);
    if (bank0.used && m_profile.alexScheme == AlexScheme::LegacyHighPassLowPass)
        o.rxLpfFrequencyHz = bank0.lpfHz;
    // User Out: RX row of the receive band / TX row of the transmit band
    // (Thetis Penny.cs); all off while stopping (the radio does the same in
    // standby).
    // The RX row by the band the receiver is TUNED to, not where its NCO
    // (the pan centre) happens to sit.
    o.openCollector = run ? openCollectorBits(m_oc, ocRxHz(filterRx),
                                              o.txFrequencyHz, o.transmitting, m_tunerStart)
                          : std::uint8_t{0};
    if (o.openCollector != m_ocLast) {
        m_ocLast = o.openCollector;
        qCInfo(lcAnanP2T) << "ANAN: User Out" << QString::number(o.openCollector, 2).rightJustified(7, QLatin1Char('0'))
                          << (o.transmitting ? (m_tunerStart ? "TUNE" : "TX") : "RX");
    }
    o.adc1GroundOnTx = m_adc1GndOnTx;
    o.psFeedbackOnAdc1 = m_psAdc == 1;
    if (run) {
        if (m_profile.alexScheme == AlexScheme::LegacyHighPassLowPass) {
            o.adc0Filter = bank0.used ? bank0.filter
                                      : legacyAlexHpfFor(static_cast<double>(m_rxHz[filterRx]));
        } else if (m_profile.alexScheme == AlexScheme::Orion2BandPass) {
            // Each ADC's bank: its receivers' filter, bypassed when they span
            // bands (build 279); ADC1 with no receiver of its own takes ADC0's.
            o.adc0Filter = bank0.used ? bank0.filter
                                      : alexRxFilterFor(static_cast<double>(m_rxHz[filterRx]));
            o.adc1Filter = bank1.used ? bank1.filter : o.adc0Filter;
            // Build 396: Thetis' "BPF ByPass on PS".
            if (psBypassesBank(0))
                o.adc0Filter = AlexRxFilter::Bypass;
            if (psBypassesBank(1))
                o.adc1Filter = AlexRxFilter::Bypass;
        } else {
            o.adc0Filter = psBypassesBank(0) ? AlexRxFilter::Bypass : bank0.filter;   // build 502
            o.adc1Filter = o.adc0Filter;
        }
        // Build 683: the Wideband Monitor's BPF Bypass, the ADC it shows.
        // (On ANAN-100/100D/200D only ADC1 has a bank; ADC2's bit then has
        // nothing to drive -- the protocol's bit, whatever the board does.)
        if (m_wbBypassMask & 1)
            o.adc0Filter = AlexRxFilter::Bypass;
        if (m_wbBypassMask & 2)
            o.adc1Filter = AlexRxFilter::Bypass;
    }
    const auto pkt =
                 buildHighPriority(run,
                                   std::span<const std::uint32_t>(words.data(),
                                                                  static_cast<std::size_t>(m_numRx)),
                                   /*bypassAdc0Filters=*/!run, /*bypassAdc1Filters=*/!run,
                                   run ? ((m_mox && m_psEnabled && m_psAdc == 0) ? m_psAttDb : m_adcAtt[0]) : 0,
                                   run ? ((m_mox && m_psEnabled && m_psAdc == 1) ? m_psAttDb : m_adcAtt[1]) : 0, o);
    sendDatagram(*m_sock, pkt, m_txHost, kHighPriorityPort, m_counters.txBytes, m_counters.txPackets);
    if (run)
        publishP2AttNow();   // build 751
}

void P2Transport::onKeepaliveTick()
{
    // Radio PTT released without a final "PTT off" status packet.
    if (m_radioPtt && !m_mox && m_sinceStatus.isValid()
        && m_sinceStatus.elapsed() > kPttStaleMs) {
        m_pttLine = false;
        m_paddleDown = false;
        if (m_paddleHold && !m_paddleHangTimer->isActive())
            m_paddleHangTimer->start(std::max(10, m_cwKeyer.hangMs));
        if (!m_paddleHold && !m_hostPaddleDown && !m_hostPaddleHold) {
            m_radioPtt = false;
            qCInfo(lcAnanP2T) << "ANAN P2 transport: radio PTT off (status stopped)";
            emit radioPttChanged(false);
        }
    }
    if (m_run && m_configured && m_p1) {
        // Thetis retries the start until EP6 flows (5 x 10 ms); here every
        // keepalive until I/Q or the connect timeout.
        if (!m_up && ++m_p1StartTries <= 20)
            sendP1Start();
    } else if (m_run && m_configured) {
        sendHighPriority(true);
    }
    // (06.11, build 178) The build-173 re-send of the whole configuration
    // while I/Q was missing is gone: Thetis does not watch the radio either.
    // A session is started (configureSession) and stopped (stop) as a whole.
    publishLinkCounters();
    if (!m_mox)   // build 231: while transmitting, m_meterTimer publishes (40 ms)
        publishTelemetry();
}

void P2Transport::handleBufferStatus(std::span<const std::uint8_t> b)
{
    // Odyssey firmware (02.11): TX FIFO 8192 samples, starts at 6144 (3/4),
    // zeros + refill to 3/4 after running empty, overflow counted; speaker
    // FIFO 2048. Counters are 16 bit, wrap, reset when the host stops the
    // radio -- differences are what count.
    // Layout version in byte 47: 1 = buffers (bytes 32-46), 3 = also the
    // link counters (bytes 16-27, test firmware 02.11).
    if (b.size() < 48 || b[47] == 0x00 || b[47] > 0x0f)
        return;
    const int layout = b[47];
    auto be16 = [&](std::size_t i) { return std::uint16_t((std::uint16_t(b[i]) << 8) | b[i + 1]); };
    auto& f = m_fpgaBuf;
    const int txLevel = be16(32);
    const std::uint16_t txEmpty = be16(34), txOvf = be16(36);
    const int spkLevel = be16(38);
    const std::uint16_t spkFade = be16(40), spkEmptyRd = be16(42), spkOvf = be16(44);
    f.flags = b[46];
    if (!f.seen) {
        f.seen = true;
        m_bufReported = true;
        qCInfo(lcAnanP2T) << "ANAN P2 transport: radio reports its TX / speaker buffers (status layout v"
                          << layout << ")";
    }
    if (layout >= 3) {
        // 16-17 command packets not applied, 18-19 audio packets dropped,
        // 20-21 I/Q packets dropped, 24-25 frames with a bad FCS (PHY quiet),
        // 26-27 frames with PHY RXER. 16 bit, since power-on: differences.
        static constexpr std::size_t kAt[5] = {16, 18, 20, 24, 26};
        static const char* kName[5] = {"command packets not applied", "audio packets dropped",
                                       "I/Q packets dropped", "frames with a bad FCS",
                                       "frames with PHY RXER"};
        std::array<std::uint16_t, 5> now{};
        for (int k = 0; k < 5; ++k)
            now[std::size_t(k)] = be16(kAt[k]);
        if (layout >= 9) {
            // v9: the 10 MHz reference (bytes 28-29), see m_ref10M.
            const int ref = int(std::int16_t(be16(28)));
            m_ref10M.store(ref, std::memory_order_relaxed);
            const int state = ref == -32768 ? -1 : (std::abs(ref) <= 1 ? 0 : 1);
            if (state != m_ref10MState) {
                m_ref10MState = state;
                qCInfo(lcAnanP2T).nospace().noquote()
                    << "ANAN P2 transport: 10 MHz reference: "
                    << (state < 0 ? QStringLiteral("no signal at the input")
                        : state == 0 ? QStringLiteral("locked (%1 Hz)").arg(ref)
                                     : QStringLiteral("NOT locked, oscillator %1 Hz at 10 MHz = %2 ppm")
                                           .arg(ref).arg(ref * 0.1, 0, 'f', 1));
            }
        }
        if (layout >= 7) {
            // v7: the PHY's crystal against C122 (122.88 MHz, locked to the
            // 10 MHz reference when present), Hz / 8, signed.
            const int dev = int(std::int16_t(be16(53)));
            const int was = m_phyClkDev.exchange(dev, std::memory_order_relaxed);
            f.clkDevMin = std::min(f.clkDevMin, dev);
            f.clkDevMax = std::max(f.clkDevMax, dev);
            if (was == INT_MIN)
                qCInfo(lcAnanP2T).nospace().noquote()
                    << "ANAN P2 transport: PHY crystal (PHY_CLK125 vs C122): "
                    << QString::number(dev * 8.0, 'f', 0) << " Hz = " << QString::number(dev * 8.0 / 125.0, 'f', 2)
                    << " ppm (1000BASE-T allows +-50 ppm)";
        }
        if (layout >= 6) {
            // Firmware v6: the PHY's own view, read once a second. 8-9 the
            // 1000BASE-T idle error count (IEEE reg 10, accumulated), 10-11
            // the KSZ9031 receive error counter (0x15), 12 the PC's EEE
            // advertisement, 13 the 1000BASE-T status (master, local / remote
            // receiver OK).
            const std::uint16_t idle = be16(8), rxe = be16(10);
            const int eee = b[12], gs = b[13];
            const int eeeWas = m_eeeAdv.exchange(eee, std::memory_order_relaxed);
            const int gsWas = m_gbStatus.exchange(gs, std::memory_order_relaxed);
            if (eee != eeeWas)
                qCInfo(lcAnanP2T).nospace().noquote()
                    << "ANAN P2 transport: PHY: the PC's network card advertises EEE: "
                    << ((eee & 0x06) ? QStringLiteral("%1%2 -- EEE is ON at the PC")
                                           .arg((eee & 0x04) ? QStringLiteral("1000BASE-T ") : QString())
                                           .arg((eee & 0x02) ? QStringLiteral("100BASE-TX") : QString())
                                     : QStringLiteral("none"))
                    << " (byte 12 = 0x" << QString::number(eee, 16) << ")";
            if (gs != gsWas)
                qCInfo(lcAnanP2T).nospace().noquote()
                    << "ANAN P2 transport: PHY 1000BASE-T status: radio is clock " << ((gs & 0x40) ? "MASTER" : "slave")
                    << ", local receiver " << ((gs & 0x20) ? "OK" : "NOT OK") << ", remote receiver "
                    << ((gs & 0x10) ? "OK" : "NOT OK") << " (byte 13 = 0x" << QString::number(gs, 16) << ")";
            if (!f.phyBase) {
                f.phyBase = true;
                qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: PHY counters so far: 1000BASE-T idle errors " << idle
                                            << ", KSZ9031 receive errors " << rxe;
            } else {
                const unsigned di = unsigned(std::uint16_t(idle - f.phyIdle));
                const unsigned dr = unsigned(std::uint16_t(rxe - f.phyRx));
                f.dPhyIdle += di;
                f.dPhyRx += dr;
                if (di || dr)
                    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: PHY: +" << di << " 1000BASE-T idle errors, +" << dr
                                                << " KSZ9031 receive errors" << (m_mox ? " (transmitting)" : " (receiving)");
            }
            f.phyIdle = idle;
            f.phyRx = rxe;
        }
        if (!f.linkBase) {
            f.linkBase = true;
            m_linkReported.store(true, std::memory_order_relaxed);
            qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: radio link counters since its power-on: "
                                        << kName[0] << " " << now[0] << ", " << kName[1] << " " << now[1]
                                        << ", " << kName[2] << " " << now[2] << ", " << kName[3] << " "
                                        << now[3] << ", " << kName[4] << " " << now[4];
        } else {
            QStringList inc;
            for (int k = 0; k < 5; ++k) {
                const unsigned dk = unsigned(std::uint16_t(now[std::size_t(k)] - f.link[std::size_t(k)]));
                if (!dk)
                    continue;
                f.dLink[std::size_t(k)] += dk;
                m_link[std::size_t(k)].fetch_add(dk, std::memory_order_relaxed);
                inc << QStringLiteral("+%1 %2").arg(dk).arg(QLatin1String(kName[k]));
            }
            if (!inc.isEmpty())
                qCInfo(lcAnanP2T).noquote().nospace()
                    << "ANAN P2 transport: radio link: " << inc.join(QStringLiteral(", "))
                    << (m_mox ? QStringLiteral(" (transmitting, TX buffer %1 of 8192)").arg(txLevel)
                              : QStringLiteral(" (receiving)"));
        }
        f.link = now;
    }
    if (f.haveBase) {
        const auto d = [](std::uint16_t now, std::uint16_t was) { return unsigned(std::uint16_t(now - was)); };
        const unsigned dEmpty = d(txEmpty, f.txEmpty);
        const unsigned dTxOvf = d(txOvf, f.txOvf);
        const unsigned dSpkFade = d(spkFade, f.spkFade);
        const unsigned dSpkOvf = d(spkOvf, f.spkOvf);
        f.dTxEmpty += dEmpty;
        f.dTxOvf += dTxOvf;
        f.dSpkFade += dSpkFade;
        f.dSpkEmptyRd += d(spkEmptyRd, f.spkEmptyRd);
        f.dSpkOvf += dSpkOvf;
        if (m_mox) {   // the over's end (we stop feeding) is not an error
            m_bufTxEmpty.fetch_add(dEmpty, std::memory_order_relaxed);
            m_bufTxOvf.fetch_add(dTxOvf, std::memory_order_relaxed);
        }
        m_bufSpkEmpty.fetch_add(dSpkFade, std::memory_order_relaxed);
        m_bufSpkOvf.fetch_add(dSpkOvf, std::memory_order_relaxed);
        if (dTxOvf && m_mox)
            qCInfo(lcAnanP2T) << "ANAN P2 transport: the radio's TX buffer OVERFLOWED (+" << dTxOvf << "samples)";
        if (dEmpty && m_mox) {
            const qint64 now = steadyMs();
            if (now - f.lastTxEmptyLogMs >= 1000) {
                f.lastTxEmptyLogMs = now;
                qCInfo(lcAnanP2T) << "ANAN P2 transport: the radio's TX buffer ran EMPTY while transmitting (+"
                                  << dEmpty << ") -- DUC packets arrived late";
            }
        }
    }
    f.haveBase = true;
    f.txEmpty = txEmpty;
    f.txOvf = txOvf;
    f.spkFade = spkFade;
    f.spkEmptyRd = spkEmptyRd;
    f.spkOvf = spkOvf;
    m_radioTxLevelSeq.fetch_add(1, std::memory_order_release);
    if (f.flags & 0x01) {   // TX buffer running
        f.txMin = std::min(f.txMin, txLevel);
        f.txMax = std::max(f.txMax, txLevel);
    }
    if (f.flags & 0x02) {   // speaker playing
        f.spkMin = std::min(f.spkMin, spkLevel);
        f.spkMax = std::max(f.spkMax, spkLevel);
        // Build 617: hold the speaker FIFO at kSpkFifoTarget (see the header).
        const qint64 now = TxCoordinator::monotonicMs();
        m_spkLvlWinMin = std::min(m_spkLvlWinMin, spkLevel);
        if (m_spkLvlWinStartMs == 0)
            m_spkLvlWinStartMs = now;
        if (now - m_spkLvlWinStartMs >= 250) {
            const int base = m_spkLvlWinMin;
            m_spkLvlWinMin = 1 << 30;
            m_spkLvlWinStartMs = now;
            if (m_spkTrimFrames.load(std::memory_order_acquire) == 0
                && now - m_spkTrimDoneMs.load(std::memory_order_relaxed) >= 300
                && base > kSpkFifoTarget + kSpkFifoSlack && base <= 2048) {
                const int excess = base - kSpkFifoTarget;
                m_spkTrimFrames.store(excess, std::memory_order_release);
                qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: radio speaker FIFO at " << base
                    << " of 2048 frames, leaving out " << excess << " frames ("
                    << excess * 1000 / 48000 << " ms) of silence -> " << kSpkFifoTarget;
            }
        }
    }
}

void P2Transport::handleStatus(std::span<const std::uint8_t> b)
{
    ++m_statusPackets;
    m_sinceStatus.start();
    if (b.size() >= 60) {
        std::lock_guard<std::mutex> lock(m_statusSnapMutex);
        std::copy(b.begin(), b.begin() + 60, m_statusSnap.begin());
        m_statusSnapValid = true;
        m_statusSnapMs = steadyMs();
    }
    auto be16 = [&](std::size_t i) { return (int(b[i]) << 8) | int(b[i + 1]); };
    m_excRaw = be16(6);
    m_fwdRaw = be16(14);
    m_fwdAtomic.store(m_fwdRaw, std::memory_order_relaxed);
    m_driveAtomic.store(m_drive, std::memory_order_relaxed);
    m_revRaw = be16(22);
    m_fwdPeakWin = std::max(m_fwdPeakWin, m_fwdRaw);   // build 231
    m_revPeakWin = std::max(m_revPeakWin, m_revRaw);
    m_supplyRaw = be16(49);   // supply volts ADC (spec p.40)
    m_userAdc1Raw = be16(55); // User ADC1: PA drain current (Thetis getUserADC1)
    m_userAdc0Raw = be16(57); // User ADC0: PA volts (Thetis getUserADC0)
    if (m_profile.odysseyStatus)
        handleBufferStatus(b);
    const bool overload = (b[5] & 0x01) != 0;   // ADC0
    const bool ptt = (b[4] & 0x01) != 0;
    const bool paddle = effectiveCwKeyer().enabled && (b[4] & 0x06) != 0;   // dot / dash
    if (overload)
        m_overloadInWindow = true;
    if (const unsigned ovl = b[5] & 0x03u)   // ADC0 / ADC1 overload (spec p.40)
    {
        m_ovlLatch.fetch_or(ovl, std::memory_order_acq_rel);
        m_ovlDisplay.fetch_or(ovl, std::memory_order_acq_rel);
    }
    // The log line, debounced (operator, 06.11: the status packets now come on
    // receive too, where the comparator chatters on a strong band): ON at the
    // first report, off only after kOvlLogDebounceMs without one.
    {
        const qint64 now = steadyMs();
        if (overload) {
            m_ovlLogLastMs = now;
            if (!m_ovlLogOn) {
                m_ovlLogOn = true;
                qCInfo(lcAnanP2T) << "ANAN P2 transport: ADC0 overload ON";
            }
        } else if (m_ovlLogOn && now - m_ovlLogLastMs >= kOvlLogDebounceMs) {
            m_ovlLogOn = false;
            qCInfo(lcAnanP2T) << "ANAN P2 transport: ADC0 overload off";
        }
    }
    m_adc0Overload = overload;
    m_pttLine = ptt;
    if (paddle != m_paddleDown) {
        m_paddleDown = paddle;
        if (paddle) {
            m_paddleHangTimer->stop();
            m_paddleHold = true;
        } else if (m_paddleHold) {
            m_paddleHangTimer->start(std::max(10, m_cwKeyer.hangMs));
        }
    }
    updateRadioPtt();
}

void P2Transport::timelineTick()
{
    if (m_tlPressMs < 0)
        return;
    const qint64 now = m_tlClock.elapsed();
    if (now < m_tlPressMs + 400)
        return;
    // Peak per 5 ms bucket, -50 .. +400 ms around the press.
    QString line;
    for (qint64 b = -50; b < 400; b += 5) {
        float pk = 0.0f;
        bool any = false;
        for (const auto& [t, v] : m_tlIq) {
            const qint64 rel = t - m_tlPressMs;
            if (rel >= b && rel < b + 5) {
                pk = std::max(pk, v);
                any = true;
            }
        }
        line += any ? QString::number(20.0 * std::log10(std::max(pk, 1e-9f)), 'f', 0)
                    : QStringLiteral("_");
        line += (b == -5) ? QStringLiteral(" | ") : QStringLiteral(" ");
    }
    qCInfo(lcAnanP2T).nospace()
        << "ANAN PTT timeline: host MOX at "
        << (m_tlMoxMs >= 0 ? QString::number(m_tlMoxMs - m_tlPressMs) : QStringLiteral("-"))
        << " ms after the radio's PTT; RX1 IQ peak dBFS per 5 ms from -50 ms (| = press): " << line;
    m_tlPressMs = -1;
    m_tlMoxMs = -1;
}

void P2Transport::updateRadioPtt()
{
    const bool keyed = m_pttLine || m_paddleDown || m_paddleHold
                       || m_hostPaddleDown || m_hostPaddleHold;   // build 851
    if (keyed == m_radioPtt)
        return;
    m_radioPtt = keyed;
    if (keyed && m_radioPttPressHook)
        m_radioPttPressHook();
    if (keyed && m_tlPressMs < 0 && m_tlClock.isValid()) {
        m_tlPressMs = m_tlClock.elapsed();
        m_tlMoxMs = m_mox ? m_tlPressMs : -1;
    }
    qCInfo(lcAnanP2T) << "ANAN P2 transport: radio PTT" << (keyed ? "ON" : "off")
                      << (m_paddleDown || m_paddleHold ? "(keyer)"
                          : m_hostPaddleDown || m_hostPaddleHold ? "(keyer, PC key lines)" : "");
    emit radioPttChanged(keyed);
}

void P2Transport::setCwBandInhibit(bool inhibit)
{
    if (inhibit == m_cwBandInhibit)
        return;
    m_cwBandInhibit = inhibit;
    if (inhibit) {
        clearCwxLines();   // build 850
        resetHostPaddle();   // build 851
    }
    qCInfo(lcAnanP2T) << "ANAN transport: the radio's CW keyer"
                      << (inhibit ? "disabled: the TX frequency is outside the amateur bands"
                                  : "enabled again (TX frequency inside an amateur band)");
    if (inhibit && (m_paddleDown || m_paddleHold)) {
        m_paddleDown = m_paddleHold = false;
        m_paddleHangTimer->stop();
        updateRadioPtt();
    }
    // Protocol 1 takes it from p1State() in the C&C rotation; Protocol 2 now.
    if (m_run && m_configured && !m_p1)
        sendTransmitSpecific();
}

void P2Transport::setCwKeyer(const CwKeyerOptions& k)
{
    if (k == m_cwKeyer)
        return;
    const bool paWas = paEffective();
    const bool demoWas = cwDemoActive();
    m_cwKeyer = k;
    m_rfDelayMs.store(k.rfDelayMs, std::memory_order_relaxed);   // read by the DUC sender
    qCInfo(lcAnanP2T).nospace()
        << "ANAN P2 transport: CW keyer " << (k.enabled ? "on" : "off") << " mode " << k.mode
        << " " << k.speedWpm << " WPM weight " << k.weight << " swap " << k.paddleSwap
        << " spacing " << k.strictSpacing << " sidetone " << k.sidetoneLevel << "/" << k.sidetoneHz
        << " Hz hang " << k.hangMs << " ms demo " << k.demo;
    if (!k.enabled && (m_paddleDown || m_paddleHold)) {
        m_paddleDown = m_paddleHold = false;
        m_paddleHangTimer->stop();
        updateRadioPtt();
    }
    if (!k.enabled) {
        clearCwxLines();   // build 850
        resetHostPaddle();   // build 851
    }
    if (!(m_run && m_configured))
        return;
    sendTransmitSpecific();
    if (paWas != paEffective())
        sendGeneral();
    if (demoWas != cwDemoActive())
        sendHighPriority(true);
}

void P2Transport::publishTelemetry()
{
    // Hl2Telemetry in Protocol 1's shape: the auto-gain loop counts overload
    // observations per window. One observation per millisecond, all of them
    // overloaded when the bit was set at any point in the window.
    if (!m_up)
        return;
    if (!m_telemetryClock.isValid()) {
        m_telemetryClock.start();
        return;
    }
    const int ms = static_cast<int>(m_telemetryClock.restart());
    const bool hot = m_overloadInWindow || m_adc0Overload;
    hl2::Hl2Telemetry t;
    t.adcOverload = hot;
    t.ptt = m_radioPtt;
    t.adcSamples = std::max(1, ms);
    t.adcOverloadSamples = hot ? t.adcSamples : 0;
    t.adcWindowMs = ms;
    // Directional coupler counts (Alex0). The watts/SWR conversion upstream
    // is the HL2's; ANAN calibration is a TODO (see the port journal).
    const bool hl2 = m_p1 && m_profile.boardId == kBoardIdHermesLite;   // build 725
    // ANAN fork build 875 (operator, 28.11: "the power indicator still jumps
    // to the last value at a new transmission -- we fixed that already").
    // Build 857 cleared the program's meters after the over, but the stale
    // value comes from the radio: the Odyssey-3's power meter MCU sends the
    // FPGA a peak only once per 300 ms window and holds it, so the first
    // readings of a new over are the last peak of the previous one. After the
    // key, readings equal to the last unkeyed ones count as no power, until
    // the radio reports something else or 400 ms have passed (then it is
    // really the same power). A radio with a live coupler ADC changes its
    // reading within the first window, so nothing is lost there.
    constexpr qint64 kStaleCouplerMs = 400;
    if (m_mox && !m_telemetryMox)
        m_fwdStaleUntilMs = steadyMs() + kStaleCouplerMs;
    m_telemetryMox = m_mox;
    if (!m_mox) {
        m_fwdIdleRaw = m_fwdRaw;
        m_revIdleRaw = m_revRaw;
        m_fwdStaleUntilMs = 0;
    }
    if (m_mox) {
        // Build 231: the window's highest readings -- the meter shows the
        // peaks the radio's detector reported, not whichever sample was last.
        t.forwardPowerRaw = m_fwdPeakWin >= 0 ? m_fwdPeakWin : m_fwdRaw;
        t.reversePowerRaw = m_revPeakWin >= 0 ? m_revPeakWin : m_revRaw;
        if (m_fwdStaleUntilMs != 0) {
            const bool stale = *t.forwardPowerRaw == m_fwdIdleRaw
                            && *t.reversePowerRaw == m_revIdleRaw;
            if (stale && steadyMs() < m_fwdStaleUntilMs) {
                t.forwardPowerRaw = 0;   // build 875: the previous over's peak
                t.reversePowerRaw = 0;
            } else {
                m_fwdStaleUntilMs = 0;   // fresh readings from here on
            }
        }
        // Build 725 (deskHPSDR): a Hermes-Lite 2 whose reverse reads above
        // the forward has its sense transformer the other way round.
        if (hl2 && *t.reversePowerRaw > *t.forwardPowerRaw)
            std::swap(t.forwardPowerRaw, t.reversePowerRaw);
        t.forwardPowerPeakRaw = t.forwardPowerRaw;
        t.forwardPowerSamples = 1;
    }
    m_fwdPeakWin = -1;
    m_revPeakWin = -1;
    if (hl2) {
        // Build 725: the Hermes-Lite 2's words (MI0BOT's Thetis / deskHPSDR):
        // C0 addr 1 C1/C2 the temperature (the ANANs' exciter power), addr 2
        // C3/C4 the PA current (their AIN3). No supply rail.
        if (m_excRaw > 0)
            t.temperatureRaw = m_excRaw;
        t.paAmpsRaw = m_userAdc0Raw;
    } else if (m_statusPackets > 0) {
        t.supplyVoltsRaw = m_supplyRaw;
        t.paVoltsRaw = m_userAdc0Raw;
        t.paAmpsRaw = m_userAdc1Raw;
    }
    m_overloadInWindow = false;
    emit telemetryUpdated(t);
}

void P2Transport::onWatchdogTick()
{
    if (m_up && m_sinceIq.isValid() && m_sinceIq.elapsed() > kSilenceTimeoutMs) {
        // Like MetisClient: the session stays running, and linkUp is emitted
        // again when IQ resumes.
        m_up = false;
        qCWarning(lcAnanP2T).nospace()
            << "ANAN P2 transport: no IQ for " << kSilenceTimeoutMs << " ms"
            << " (this session: " << m_counters.rxPackets << " datagrams received; Protocol 1 packets: "
            << m_p1DiagSeen << " seen, " << m_p1DiagBadSize << " not I/Q or of another size, "
            << m_p1DiagNoSync << " without a valid frame, " << m_p1DiagOneFrame << " with one frame of two)";
        // 03.11 (log 20:40:52): the host's unkey at the link loss is refused by
        // the TX coordinator (a connection reset ends the operation, cleanup
        // included), so MOX stayed set here and the radio transmitted again
        // the moment the link came back -- 14 s at the reset drive. A lost
        // link ends the transmission at the transport, always.
        unkeyOnLinkLoss();
        emit linkDown();
        // Build 377 (operator, 14.11: "after the transceiver is lost, its full
        // initialisation as at the start"). The session used to stay running
        // and take the stream back when I/Q resumed. Log 20261114-125136: the
        // radio was switched off and on; it came back in its power-on state,
        // our High Priority packets (run = 1) started it, and it streamed at
        // ITS default 48 ksps into chains built for 192 ksps -- it had never
        // been sent General / DDC Specific / TX Specific again. So the loss
        // ENDS the session: the socket is closed, nothing more is sent, and
        // the radio is found and connected again by the ordinary path
        // (discovery -> connect), which configures it from the start.
        qCInfo(lcAnanP2T) << "ANAN transport: session ended after the loss of I/Q; the radio will be"
                             " connected and configured anew when it answers discovery";
        stopSession(true);
    }
}

// ---- receive ---------------------------------------------------------------

void P2Transport::onReadyRead()
{
    // Build 376: a bounded stay. When this thread cannot keep up with the
    // radio (log 20261114-185644: 71 of 192 kS/s processed), the socket never
    // runs empty and this loop never returned to the event loop -- so the
    // queued stop() of a disconnect was never run, and the destructor, which
    // waits for it, hung the program ("main thread not responding"). After
    // 20 ms the rest is left to a queued call of this function: whatever was
    // posted to this thread meanwhile (stop, a retune) runs first.
    QElapsedTimer budget;
    budget.start();
    int taken = 0;
    while (m_sock && m_sock->hasPendingDatagrams()) {
        if ((++taken & 31) == 0 && budget.elapsed() > 20) {
            ++m_diagReadYields;
            QMetaObject::invokeMethod(this, &P2Transport::onReadyRead, Qt::QueuedConnection);
            return;
        }
        const QNetworkDatagram dg = m_sock->receiveDatagram();
        if (!dg.senderAddress().isEqual(m_p.host, QHostAddress::TolerantConversion)) {
            if (!m_warnedPorts.contains(0)) {
                m_warnedPorts.insert(0);
                qCWarning(lcAnanP2T) << "ANAN P2 transport: ignoring datagram from"
                                     << dg.senderAddress().toString() << "(radio is"
                                     << m_p.host.toString() << ")";
            }
            continue;
        }
        const QByteArray data = dg.data();
        m_counters.rxBytes += static_cast<quint64>(data.size());
        ++m_counters.rxPackets;
        handleDatagram(asBytes(data), static_cast<quint16>(dg.senderPort()));
    }
}

void P2Transport::handleDatagram(std::span<const std::uint8_t> bytes, quint16 senderPort)
{
    // Protocol 1 (build 194): every packet starts EF FE.
    if (bytes.size() >= 11 && bytes[0] == 0xEF && bytes[1] == 0xFE) {
        if (bytes[2] == 0x01) {
            if (m_p1 && m_configured) {
                if (bytes[3] == 0x04)
                    handleP1Ep4(bytes);
                else
                    handleP1Ep6(bytes);
            }
            return;
        }
        if ((bytes[2] == 0x02 || bytes[2] == 0x03) && !m_configured && !m_p1Pending) {
            if (const auto r1 = hl2::parseDiscoveryReply(bytes)) {
                const auto id = p2BoardIdForP1(r1->boardId, r1->gatewareVersion);
                qCInfo(lcAnanP2T).nospace()
                    << "ANAN transport: Protocol 1 Discovery reply: board " << int(r1->boardId)
                    << ", gateware " << int(r1->gatewareVersion) << ", "
                    << (r1->streaming ? "STREAMING (busy with a session)" : "idle")
                    << (id ? QString() : QStringLiteral(" -- not a board this backend drives"));
                if (id) {
                    noteProtocol1Host(m_p.host.toIPv4Address(), true);
                    m_p1Gateware = r1->gatewareVersion;
                    m_p1Pending = boardProfileFor(*id);
                    // A Protocol 2 reply would have come first; give it until
                    // the board wait ends, then run Protocol 1.
                }
            }
        }
        return;
    }
    const auto frame = parseDdcFrame(bytes);
    if (!frame) {
        if (const auto reply = senderPort == kRadioPort ? parseDiscoveryReply(bytes)
                                                        : std::nullopt) {
            if (!m_configured) {
                // ANAN-100D port (06.11): 0x03 = the radio says it already
                // streams to a session (a host it never got "run = 0" from).
                qCInfo(lcAnanP2T).nospace()
                    << "ANAN P2 transport: Discovery reply: board " << int(reply->boardId) << ", firmware "
                    << int(reply->firmwareVer) << ", protocol " << int(reply->protoVersionRaw) << ", "
                    << int(reply->numDdc) << " DDC, "
                    << (reply->streaming ? "STREAMING (busy with a session)" : "idle");
                m_discoveredNumDdc = reply->numDdc;
                m_p1 = false;
                noteProtocol1Host(m_p.host.toIPv4Address(), false);
                const auto profile = boardProfileFor(reply->boardId);
                configureSession(profile.value_or(defaultBoardProfile()), profile.has_value());
            }
        }
        if (senderPort == 1025 && bytes.size() == kHighPriorityStatusBytes)
            handleStatus(bytes);
        if ((senderPort == kWidebandBasePort || senderPort == kWidebandBasePort + 1)
            && bytes.size() == static_cast<std::size_t>(kWidebandPacketBytes)) {
            ++m_diagWbPkts[static_cast<std::size_t>(senderPort - kWidebandBasePort)];
            handleWideband(senderPort - kWidebandBasePort, bytes);
            return;
        }
        if (bytes.size() == static_cast<std::size_t>(kWidebandPacketBytes)) {
            ++m_diagWbOther;   // wideband-sized, from an unexpected port
            m_diagWbOtherPort = senderPort;
        }
        if (senderPort == kMicFromRadioPort && bytes.size() == kMicPacketBytes) {
            m_sinceMic.start();
            ++m_diagMicPkts;
            auto mic = parseMicPacket(bytes);
            if (!mic.empty()) {
                // The mic stream is the transmitter's clock: a lost packet
                // still stands for 1.33 ms of the radio's time. Up to 16 in a
                // row are replaced by silence (time kept, TXA in step); a
                // larger jump is a restart, not a loss.
                const std::uint32_t seq = (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16)
                                          | (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
                if (m_micSeqValid) {
                    const std::uint32_t lost = seq - m_micSeqNext;
                    if (lost > 0 && lost <= 16) {
                        m_diagMicLost += lost;
                        mic.insert(mic.begin(), std::size_t(lost) * kCodecFramesPerPacket, 0.0f);
                    }
                }
                m_micSeqNext = seq + 1;
                m_micSeqValid = true;
                m_micArrived.fetch_add(mic.size() / kCodecFramesPerPacket, std::memory_order_relaxed);
                emit codecMicReady(mic);
            }
        }
        return;
    }

    // Build 466: RX3 / RX4 on DDC0 / DDC1 (ddcWrap()) -- theirs unless
    // PureSignal holds those DDCs (psHoldsLowDdcs()).
    const int wrap = ddcWrap();
    updateHeldMask();
    if (wrap > 0 && senderPort >= kDdc0DefaultPort && senderPort < kDdc0DefaultPort + 2) {
        const int hw = senderPort - kDdc0DefaultPort;
        if (psHoldsLowDdcs()) {
            // PureSignal: the synchronised DDC0/DDC1 pairs (feedback + TX DAC).
            if (hw == 0 && m_psEnabled && m_mox && m_psSink
                && TxCoordinator::monotonicMs() >= m_psAcceptFromMs.load(std::memory_order_relaxed)) {
                m_psTxBuf.clear();
                m_psRxBuf.clear();
                if (decodePsPairs(bytes, m_psTxBuf, m_psRxBuf) && !m_psRxBuf.empty())
                    deliverPsPairs();
            }
            return;
        }
        int rxOnDdc = -1;
        for (int i = 0; i < m_numRx; ++i) {
            if (hwDdcOfRx(i) == hw) {
                rxOnDdc = i;
                break;
            }
        }
        if (rxOnDdc < 0)
            return;   // no receiver there (fewer than 3 / 4 running)
    } else if (senderPort == kDdc0DefaultPort && m_profile.rxDdcBase > 0) {
        // PureSignal: the synchronised DDC0/DDC1 pairs (feedback + TX DAC).
        if (m_psEnabled && m_mox && m_psSink) {
            m_psTxBuf.clear();
            m_psRxBuf.clear();
            if (decodePsPairs(bytes, m_psTxBuf, m_psRxBuf) && !m_psRxBuf.empty()) {
                deliverPsPairs();
            }
        }
        return;
    }
    std::optional<int> rx;
    if (wrap > 0) {
        const int hw = senderPort - kDdc0DefaultPort;
        for (int i = 0; i < m_numRx && hw >= 0 && hw < wrap; ++i) {
            if (hwDdcOfRx(i) == hw) {
                rx = i;
                break;
            }
        }
    } else {
        const std::uint16_t basePort = static_cast<std::uint16_t>(kDdc0DefaultPort + m_profile.rxDdcBase);
        rx = ddcIndexForSenderPort(senderPort, m_numRx, basePort);
    }
    if (!rx) {
        if (!m_warnedPorts.contains(senderPort)) {
            m_warnedPorts.insert(senderPort);
            qCWarning(lcAnanP2T) << "ANAN P2 transport: ignoring IQ from port" << senderPort;
        }
        return;
    }
    const std::size_t slot = static_cast<std::size_t>(*rx);

    if (m_expectedSeq[slot] && frame->seq != *m_expectedSeq[slot]) {
        const std::uint32_t lost = frame->seq - *m_expectedSeq[slot];
        ++m_dropTotal;
        ++m_diagGaps;
        if (lost < 100000)
            m_diagLost += lost;
        emit dropsUpdated(m_dropTotal);
        emit rxSequenceGap(lost);
    }
    m_expectedSeq[slot] = frame->seq + 1;

    m_scratch.clear();
    decodeIq(*frame, m_scratch);
    if (slot == 0) {
        m_diagSamples += m_scratch.size();
        diagTick();
        float pk = 0.0f;
        for (const auto& v : m_scratch)
            pk = std::max(pk, std::abs(v));
        if (!m_tlClock.isValid())
            m_tlClock.start();
        const qint64 now = m_tlClock.elapsed();
        m_tlIq.emplace_back(now, pk);
        while (!m_tlIq.empty() && m_tlIq.front().first < now - 1000)
            m_tlIq.pop_front();
        timelineTick();
    }
    m_pending[slot].push_back(m_scratch);
    if (m_pending[slot].size() > kMaxAheadPackets)
        m_pending[slot].pop_front();

    m_sinceIq.restart();
    if (!m_up && slot == 0) {
        m_up = true;
        m_connectTimer->stop();
        noteBindWorked();
        emit linkUp();
    }
    deliverAlignedBlocks();
}

void P2Transport::deliverAlignedBlocks()
{
    // One block per receiver per emission, as Hl2Backend expects from EP6.
    // The DDCs run at one rate, so their packets arrive in lockstep.
    // Build 466: a receiver PureSignal holds (heldReceiverMask()) sends nothing
    // -- its block is silence the size of the others', so the rest keep going;
    // its queue starts afresh, and its sequence too, when it is given back.
    const std::uint32_t held = m_heldMask.load(std::memory_order_relaxed);
    if (held != m_heldSeen) {
        for (int i = 0; i < m_numRx; ++i) {
            if ((held ^ m_heldSeen) & (1u << i)) {
                m_pending[static_cast<std::size_t>(i)].clear();
                m_expectedSeq[static_cast<std::size_t>(i)].reset();
            }
        }
        m_heldSeen = held;
    }
    for (;;) {
        std::size_t blockLen = 0;
        for (int i = 0; i < m_numRx; ++i) {
            if (held & (1u << i))
                continue;
            if (m_pending[static_cast<std::size_t>(i)].empty())
                return;
            if (!blockLen)
                blockLen = m_pending[static_cast<std::size_t>(i)].front().size();
        }
        if (!blockLen)
            return;   // every receiver held: nothing clocks them
        for (int i = 0; i < m_numRx; ++i) {
            if (held & (1u << i))
                m_pending[static_cast<std::size_t>(i)].emplace_back(blockLen);
        }
        m_out.resize(static_cast<std::size_t>(m_numRx));
        for (int i = 0; i < m_numRx; ++i) {
            auto& q = m_pending[static_cast<std::size_t>(i)];
            m_out[static_cast<std::size_t>(i)] = std::move(q.front());
            q.pop_front();
        }
        emit iqBlockReady(m_out[0]);
        emit iqBlocksReady(m_out);
    }
}

void P2Transport::publishLinkCounters()
{
    if (!m_linkClock.isValid() || m_linkClock.elapsed() < kLinkPublishMs)
        return;
    m_linkClock.restart();
    m_counters.drops = m_dropTotal;
    // DUC and speaker packets leave from their own threads (sendNative).
    m_counters.txBytes += m_nativeTxBytes.exchange(0, std::memory_order_relaxed);
    m_counters.txPackets += m_nativeTxPackets.exchange(0, std::memory_order_relaxed);
    if (m_sock)
        m_counters.localEndpoint = QStringLiteral("%1:%2")
                                       .arg(m_sock->localAddress().toString())
                                       .arg(m_sock->localPort());
    emit linkCountersUpdated(m_counters);
}

void P2Transport::diagTick()
{
    if (!m_diagClock.isValid()) {
        m_diagClock.start();
        return;
    }
    const qint64 ms = m_diagClock.elapsed();
    if (ms < kDiagPeriodMs)
        return;
    qCInfo(lcAnanP2T).nospace().noquote()
        << "ANAN diag: DDC" << m_profile.rxDdcBase << " " << qRound(m_diagSamples * 1000.0 / ms)
        << " samples/s (configured " << rateKsps() * 1000 << "), " << m_diagGaps
        << " gaps (" << m_diagLost << " packets lost), " << m_numRx << " rx, RX1 "
        << m_rxHz[0] << " Hz, att " << m_adcAtt[0] << "/" << m_adcAtt[1] << " dB, status packets "
        << m_statusPackets << (m_adc0Overload ? ", ADC0 OVERLOAD" : "")
        << "; codec: mic " << m_diagMicPkts << " pkts, speaker " << m_diagSpkPkts.load() << " pkts ("
        << m_diagSpkSilent << " silent), " << m_diagSpkSubmits << " blocks in, peak "
        << m_diagSpkPeak << ", mic lost " << m_diagMicLost << ", native send fails "
        << m_nativeSendFails.exchange(0, std::memory_order_relaxed) << ", queue " << [this] {
               std::lock_guard<std::mutex> lock(m_spkMutex);
               return m_spkQueue.size();
           }()
        << ", read loop yields " << std::exchange(m_diagReadYields, 0)
        // Build 600: the AetherRX feed in front of the radio speaker.
        << [] {
               auto& feed = AnanCodecFeed::instance();
               std::size_t lo = 0, hi = 0;
               if (!feed.wanted() || !feed.takeDepthRange(lo, hi))
                   return QString();
               return QStringLiteral("; AetherRX feed %1..%2 ms, underruns %3, trims %4, "
                                     "silence drops %5 (last %6 ms)")
                   .arg(lo * 1000 / AnanCodecFeed::kRate)
                   .arg(hi * 1000 / AnanCodecFeed::kRate)
                   .arg(feed.underruns())
                   .arg(feed.trims())
                   .arg(feed.silenceDrops())
                   .arg(feed.lastSilenceDropFrames() * 1000 / AnanCodecFeed::kRate);
           }()
        << (m_wb.enableMask || m_diagWbPkts[0] || m_diagWbPkts[1] || m_diagWbOther
                ? QStringLiteral("; wideband mask %1: port 1027 (ADC1) %2 pkts / %3 frames, "
                                 "port 1028 (ADC2) %4 pkts / %5 frames, other ports %6 (last %7)")
                      .arg(int(m_wb.enableMask)).arg(m_diagWbPkts[0]).arg(m_diagWbFrames[0])
                      .arg(m_diagWbPkts[1]).arg(m_diagWbFrames[1]).arg(m_diagWbOther).arg(m_diagWbOtherPort)
                : QString());
    // 03.11: the radio sends no status while receiving (log 18:47: none
    // before the first MOX, none after the last) -- then say so instead of
    // a line of zeros that looks like a clean link.
    const bool statusNow = m_statusPackets != m_diagStatusSeen;
    m_diagStatusSeen = m_statusPackets;
    if (m_fpgaBuf.seen && !statusNow) {
        qCInfo(lcAnanP2T) << "ANAN diag radio buffers: no status packets from the radio in the last 5 s"
                          << "(none while receiving) -- no link / PHY counters for this period";
    } else if (m_fpgaBuf.seen) {
        auto& f = m_fpgaBuf;
        qCInfo(lcAnanP2T).nospace().noquote()
            << "ANAN diag radio buffers: TX "
            << (f.txMax >= 0 ? QStringLiteral("%1..%2").arg(f.txMin).arg(f.txMax) : QStringLiteral("idle"))
            << " of 8192, ran empty " << f.dTxEmpty << ", overflow " << f.dTxOvf << " samples; speaker "
            << (f.spkMax >= 0 ? QStringLiteral("%1..%2").arg(f.spkMin).arg(f.spkMax) : QStringLiteral("idle"))
            << " of 2048, fades " << f.dSpkFade << ", empty reads " << f.dSpkEmptyRd << ", overflow " << f.dSpkOvf
            << ", silent packets left out " << m_spkTrimmedPkts.exchange(0, std::memory_order_relaxed)
            << " (held at " << kSpkFifoTarget << ")"
            << (f.linkBase ? QStringLiteral("; link: commands not applied %1, audio dropped %2, I/Q dropped %3, "
                                            "bad FCS %4, PHY RXER %5")
                                 .arg(f.dLink[0]).arg(f.dLink[1]).arg(f.dLink[2]).arg(f.dLink[3]).arg(f.dLink[4])
                           : QString())
            << (f.phyBase ? QStringLiteral("; PHY: idle errors %1, receive errors %2").arg(f.dPhyIdle).arg(f.dPhyRx)
                          : QString())
            << (f.clkDevMax != INT_MIN ? QStringLiteral("; PHY crystal %1..%2 ppm")
                                             .arg(f.clkDevMin * 8.0 / 125.0, 0, 'f', 2)
                                             .arg(f.clkDevMax * 8.0 / 125.0, 0, 'f', 2)
                                       : QString());
        f.clkDevMin = INT_MAX;
        f.clkDevMax = INT_MIN;
        f.dLink = {};
        f.dPhyIdle = f.dPhyRx = 0;
        f.txMin = f.spkMin = 1 << 30;
        f.txMax = f.spkMax = -1;
        f.dTxEmpty = f.dTxOvf = f.dSpkFade = f.dSpkEmptyRd = f.dSpkOvf = 0;
    }
    m_diagWbPkts = {{0, 0}};
    m_diagWbFrames = {{0, 0}};
    m_diagWbOther = 0;
    m_diagMicPkts = m_diagSpkPkts = m_diagSpkSilent = m_diagSpkSubmits = 0;
    m_diagSpkPeak = 0;
    m_diagClock.restart();
    m_diagSamples = m_diagGaps = m_diagLost = 0;
}

// ---- receiver commands -----------------------------------------------------

void P2Transport::setRxFrequencyHz(std::uint32_t hz)
{
    setRxFrequencyHz(0, hz);
}

void P2Transport::setRxFrequencyHz(int rxIndex, std::uint32_t hz)
{
    if (rxIndex < 0 || rxIndex >= kMaxDdcs)
        return;
    m_rxHz[static_cast<std::size_t>(rxIndex)] = hz;
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setRxTunedHz(int rxIndex, std::uint32_t hz)
{
    if (rxIndex < 0 || rxIndex >= kMaxDdcs)
        return;
    const auto i = static_cast<std::size_t>(rxIndex);
    if (m_rxTunedHz[i] == hz)
        return;
    const int bandBefore = ocBandIndex(ocRxHz(i));
    const BankChoice bank0Before = bankFor(0);
    const BankChoice bank1Before = bankFor(1);
    m_rxTunedHz[i] = hz;
    // Only a band change can move the outputs or the filter banks (build
    // 280: the banks follow the tuned frequencies); the frequency itself is
    // not on the wire.
    const BankChoice bank0 = bankFor(0);
    const BankChoice bank1 = bankFor(1);
    const bool banksMoved = bank0.filter != bank0Before.filter || bank0.spanned != bank0Before.spanned
        || bank1.filter != bank1Before.filter || bank1.spanned != bank1Before.spanned
        || p1AlexLpfBitsFor(bank0.lpfHz) != p1AlexLpfBitsFor(bank0Before.lpfHz);
    if ((ocBandIndex(ocRxHz(i)) != bandBefore || banksMoved) && m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setFilterOwnerRx(int rxIndex)
{
    if (rxIndex == m_filterOwnerRx)
        return;
    m_filterOwnerRx = rxIndex;
    qCInfo(lcAnanP2T) << "ANAN: receive filter bank" << (rxIndex >= 0 ? "held by receiver" : "shared again")
                      << (rxIndex >= 0 ? rxIndex + 1 : 0);
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setSampleRate(hl2::SampleRate rate)
{
    m_p.sampleRate = rate;
    if (m_run && m_configured)
        sendDdcSpecific();
    // The stream's cadence changes underneath the receivers.
    m_expectedSeq.fill(std::nullopt);
    for (auto& q : m_pending)
        q.clear();
}

void P2Transport::setReceiverCount(int count)
{
    hl2::MetisClient::Params p = m_p;
    p.numRx = count;
    int n = std::clamp(effectiveNumRx(p), 1, kMaxDdcs);
    if (m_discoveredNumDdc > 0)
        n = std::min(n, receiversForBoard(m_profile, m_discoveredNumDdc));
    if (m_p1)
        n = std::min(n, m_p1Plan.maxRx);   // build 468: the Protocol 1 DDC plan
    if (n == m_numRx)
        return;
    // New receivers start on receiver 0's frequency until Hl2Backend tunes them.
    for (int i = m_numRx; i < n; ++i)
        m_rxHz[static_cast<std::size_t>(i)] = m_rxHz[0];
    m_numRx = n;
    m_p.numRx = n;
    m_expectedSeq.fill(std::nullopt);
    for (auto& q : m_pending)
        q.clear();
    if (m_run && m_configured) {
        sendDdcSpecific();
        sendHighPriority(true);
    }
}

void P2Transport::setLnaGainDb(int db)
{
    m_p.lnaGainDb = db;
    // ANAN-100D port: the attenuators are per ADC now (setAdcAttenuation);
    // the HL2 "LNA gain" no longer drives them.
}

void P2Transport::setBandFilter(int ocFilterByte)
{
    // N2ADR filter board byte: no meaning on ANAN; Alex follows the frequency.
    m_p.ocFilterByte = static_cast<std::uint8_t>(ocFilterByte);
}

void P2Transport::setDitherRandomBits(bool dither, bool random)
{
    // Build 439: the radio-wide pair of the hardware page is not used on
    // ANAN any more -- per ADC from the ANT menus (setAdcDitherRandom).
    m_dither = dither;
    m_random = random;
}

void P2Transport::setAdcDitherRandom(int ditherMask, int randomMask)
{
    ditherMask &= 0x07;
    randomMask &= 0x07;
    if (ditherMask == m_ditherMask && randomMask == m_randomMask)
        return;
    m_ditherMask = ditherMask;
    m_randomMask = randomMask;
    qCInfo(lcAnanP2T).nospace() << "ANAN transport: Dither ADC1 " << ((ditherMask & 1) ? "on" : "off")
                                << ", ADC2 " << ((ditherMask & 2) ? "on" : "off") << "; Random ADC1 "
                                << ((randomMask & 1) ? "on" : "off") << ", ADC2 "
                                << ((randomMask & 2) ? "on" : "off")
                                << (m_p1 ? " (Protocol 1: one pair for the radio, ADC1's)" : "");
    if (m_run && m_configured && !m_p1)
        sendDdcSpecific();   // Protocol 1: the next C&C round carries it
}

// ANAN fork build 279 (operator, 11.11: "WIDE means the filters are bypassed,
// so that both receivers work"). Until now the bank followed the first
// receiver on its ADC and a second receiver on another band heard through
// the wrong filter -- for the classic Alex even through a low-pass below its
// band. Now: one filter for all receivers on the ADC when they agree; when
// they do not, the bank is bypassed and the low-pass in the receive path
// (classic Alex) is set for the highest of their frequencies, which passes
// them all. Which ADCs have a bank, by the scheme:
//  - Orion2BandPass: ADC0, and ADC1 when the board has the second bank;
//  - LegacyHighPassLowPass: ADC0 only (ADC1 has no Alex filters);
//  - SaturnBandPass (as driven here): one selection for both ADCs.
// Build 280 (operator, 11.11: "any receiver can work with any ADC, the
// filters are wired to the ADC; look only at which ADC is in use and on
// which band -- the receiver's number means nothing"). A bank is decided by
// the receivers whose ADC (effectiveAdc: a one-ADC board has ADC1 only) it
// sits ahead of, in any order: one ADC -> every receiver shares bank 0; two
// ADCs with one bank (100D/200D) -> ADC1 by these rules, ADC2 never filtered
// and never WIDE; two ADCs with two banks (7000DLE/8000DLE/G2: band-pass,
// their own bits and edges, alexRxFilterFor) -> each ADC by itself.
P2Transport::BankChoice P2Transport::bankFor(int adc) const
{
    BankChoice b;
    const AlexScheme scheme = m_profile.alexScheme;
    const bool shared = scheme != AlexScheme::Orion2BandPass
        && scheme != AlexScheme::LegacyHighPassLowPass;
    const bool legacy = scheme == AlexScheme::LegacyHighPassLowPass;
    if (legacy && adc != 0)
        return b;
    if (scheme == AlexScheme::Orion2BandPass && adc == 1 && !m_profile.hasAdc1FilterBank)
        return b;
    const auto filterOf = [legacy](double hz) {
        return legacy ? legacyAlexHpfFor(hz) : alexRxFilterFor(hz);
    };
    const auto keyOf = [&](double hz) {
        return static_cast<int>(filterOf(hz)) * 256
            + (legacy ? static_cast<int>(p1AlexLpfBitsFor(hz)) : 0);
    };
    // Build 388: the owner of the bank (the FTX receiver) decides it alone.
    if (m_filterOwnerRx >= 0 && m_filterOwnerRx < m_numRx
        && (shared || effectiveAdc(m_filterOwnerRx) == adc)) {
        const double hz = ocRxHz(static_cast<std::size_t>(m_filterOwnerRx));
        b.used = true;
        b.filter = filterOf(hz);
        b.lpfHz = hz;
        return b;
    }
    int key = -1;
    for (int i = 0; i < m_numRx; ++i) {
        if (!shared && effectiveAdc(i) != adc)
            continue;
        // The frequency the receiver is TUNED to (build 280), as the WIDE
        // indicator reads it -- not the NCO / pan centre.
        const double hz = ocRxHz(static_cast<std::size_t>(i));
        if (!b.used) {
            b.used = true;
            b.filter = filterOf(hz);
            b.lpfHz = hz;
            key = keyOf(hz);
            continue;
        }
        b.lpfHz = std::max(b.lpfHz, hz);
        if (keyOf(hz) != key)
            b.spanned = true;
    }
    if (b.spanned)
        b.filter = AlexRxFilter::Bypass;
    return b;
}

// Build 277: the ADC a receiver actually listens to -- a board with one ADC
// has no ADC2, whatever was asked for. Every filter and DDC rule reads this,
// so a stored "ADC2" cannot hand the ADC1 filters to another receiver there.
int P2Transport::effectiveAdc(int rx) const
{
    if (rx < 0 || rx >= kMaxDdcs)
        return 0;
    return std::min(m_rxAdc[static_cast<std::size_t>(rx)], std::max(0, m_profile.numAdcs - 1));
}

void P2Transport::setReceiverAdc(int rx, int adc)
{
    if (rx < 0 || rx >= kMaxDdcs)
        return;
    adc = adc == 1 ? 1 : 0;
    if (m_rxAdc[static_cast<std::size_t>(rx)] == adc)
        return;
    m_rxAdc[static_cast<std::size_t>(rx)] = adc;
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: RX" << (rx + 1) << " <- ADC" << (adc + 1);
    if (m_run && m_configured) {
        sendDdcSpecific();
        // Build 280: the banks are decided by who listens to which ADC.
        sendHighPriority(true);
    }
}

void P2Transport::setRxAntenna(int antenna)
{
    antenna = std::clamp(antenna, 0, 2);
    if (antenna == m_rxAnt)
        return;
    m_rxAnt = antenna;
    qCInfo(lcAnanP2T).noquote() << QStringLiteral("ANAN: RX antenna ANT%1").arg(antenna + 1);
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setTunerStart(bool on)
{
    if (on == m_tunerStart)
        return;
    m_tunerStart = on;
    qCInfo(lcAnanP2T) << "ANAN: tuner start bit" << (on ? "ON" : "off");
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setXvtrTx(bool on)
{
    if (on == m_xvtrTx)
        return;
    m_xvtrTx = on;
    qCInfo(lcAnanP2T) << "ANAN: transverter port" << (on ? "ON (PA disabled, XVTR T/R relay enabled)"
                                                           : "off (PA enabled)");
    if (m_run && m_configured && !m_p1) {
        sendGeneral();
        sendHighPriority(true);
    }
}

void P2Transport::setPsAttFollowsDrive(bool on)
{
    if (on == m_psAttFollowDrive)
        return;
    m_psAttFollowDrive = on;
    m_psDriveRef = on ? double(m_drive) : 0.0;
}

// Build 838: AnanPureSignal set an attenuation that belongs to no drive in
// use now (TUNE's 31 dB start, the voice value given back after TUNE): the
// next drive becomes its reference, so it is not moved by that drive.
void P2Transport::resetPsDriveRef()
{
    m_psDriveRef = 0.0;
}

void P2Transport::setPureSignal(bool enabled, int feedbackSource, int attDb)
{
    feedbackSource = std::clamp(feedbackSource, 0, 2);
    attDb = std::clamp(attDb, 0, 31);
    // Not on the boards whose receivers sit on DDC0/DDC1 (BoardProfile::
    // psSupported); the profile is the default ANGELIA one until the
    // Discovery reply has configured the session.
    if (enabled && m_configured && !m_profile.psSupported) {
        qCWarning(lcAnanP2T) << "ANAN: PureSignal is not available on the" << m_profile.modelName;
        enabled = false;
    }
    if (enabled == m_psEnabled && feedbackSource == m_psSource && attDb == m_psAttDb)
        return;
    const bool changedOnOff = enabled != m_psEnabled;
    {
        std::lock_guard<std::mutex> lock(m_txMutex);   // read by the DUC sender
        m_psEnabled = enabled;
    }
    m_psSource = feedbackSource;
    // Build 837: an attenuation set from outside (auto-attenuate, the PS
    // window) belongs to the drive in use now; while no drive is out (a
    // pause) the reference stays the last drive's.
    if (attDb != m_psAttDb && m_drive > 0)
        m_psDriveRef = m_drive;
    m_psAttDb = attDb;
    m_psAttNow.store(attDb, std::memory_order_relaxed);
    if (changedOnOff)
        qCInfo(lcAnanP2T) << "ANAN: PureSignal" << (enabled ? "on" : "off") << "feedback"
                          << feedbackSource << "att" << attDb << "dB";
    if (!(m_run && m_configured))
        return;
    sendTransmitSpecific();
    if (changedOnOff && m_mox)
        sendDdcSpecific();
    sendHighPriority(true);
}

bool P2Transport::ducSnapshot(std::vector<double>& iq) const
{
    std::lock_guard<std::mutex> lock(m_ducSnapMutex);
    if (m_ducSnapFill < kDucSnap)
        return false;
    iq.resize(std::size_t(2 * kDucSnap));
    for (int i = 0; i < kDucSnap; ++i) {
        const int k = (m_ducSnapPos + i) % kDucSnap;
        iq[std::size_t(2 * i)] = m_ducSnap[std::size_t(2 * k)];
        iq[std::size_t(2 * i + 1)] = m_ducSnap[std::size_t(2 * k + 1)];
    }
    return true;
}

void P2Transport::setPsLoopbackTest(bool on)
{
    if (on == m_psLoopback)
        return;
    m_psLoopback = on;
    qCInfo(lcAnanP2T) << "ANAN: PureSignal loopback test" << (on ? "ON: DDC0 = DAC (ADC 2), DDC1 = DAC"
                                                                 : "off: DDC0 = feedback ADC");
    if (m_run && m_configured && m_mox && m_psEnabled)
        sendDdcSpecific();
}

void P2Transport::setPsFeedbackAdc(int adc)
{
    adc = std::clamp(adc, 0, 1);
    if (adc == m_psAdc)
        return;
    m_psAdc = adc;
    qCInfo(lcAnanP2T) << "ANAN: PureSignal feedback ADC" << adc + 1;
    if (!(m_run && m_configured))
        return;
    sendTransmitSpecific();
    if (m_mox && m_psEnabled)
        sendDdcSpecific();
    sendHighPriority(true);
}

P2Transport::StatusSnapshot P2Transport::statusSnapshot() const
{
    StatusSnapshot s;
    {
        std::lock_guard<std::mutex> lock(m_statusSnapMutex);
        s.valid = m_statusSnapValid;
        s.b = m_statusSnap;
        if (m_statusSnapValid)
            s.ageMs = steadyMs() - m_statusSnapMs;
    }
    s.count = m_radioTxLevelSeq.load(std::memory_order_acquire);
    s.ducPacketsSent = m_ducSentTotal.load(std::memory_order_relaxed);
    s.mox = m_moxAtomic.load(std::memory_order_relaxed);
    s.drive = m_driveAtomic.load(std::memory_order_relaxed);
    return s;
}

void P2Transport::setWideband(int adcMask, int updateMs)
{
    WidebandOptions wb;
    wb.enableMask = static_cast<std::uint8_t>(adcMask & 0x03);
    wb.updateMs = static_cast<std::uint8_t>(std::clamp(updateMs, 20, 255));
    wb.packetsPerFrame = 32;
    if (wb.enableMask == m_wb.enableMask && wb.updateMs == m_wb.updateMs)
        return;
    m_wb = wb;
    for (auto& n : m_wbNext)
        n = -1;
    qCInfo(lcAnanP2T) << "ANAN: wideband" << (wb.enableMask ? "on" : "off")
                      << "mask" << int(wb.enableMask) << "every" << int(wb.updateMs) << "ms";
    if (m_p1) {
        // Protocol 1: bit 1 of the start command runs the wide bandscope
        // (EP4), which carries ADC0 only.
        if ((wb.enableMask & 0x02) && !m_p1WbAdc2Logged) {
            m_p1WbAdc2Logged = true;
            qCWarning(lcAnanP2T) << "ANAN P1 transport: the Protocol 1 wide bandscope carries ADC1 only";
        }
        m_wbBuf[0].clear();
        m_p1WbClock.invalidate();
        if (m_run && m_configured && m_sock)
            sendDatagram(*m_sock, hl2::metisCommand((wb.enableMask & 0x01) ? 0x03 : 0x01), m_txHost,
                         kRadioPort, m_counters.txBytes, m_counters.txPackets);
        return;
    }
    if (m_run && m_configured)
        sendGeneral();
}

void P2Transport::handleWideband(int adc, std::span<const std::uint8_t> bytes)
{
    if (adc < 0 || adc > 1 || !(m_wb.enableMask & (1u << adc)))
        return;
    std::uint32_t seq = 0;
    if (!parseWidebandPacket(bytes, seq, m_wbScratch.data()))
        return;
    if (!m_wbLogged[static_cast<std::size_t>(adc)]) {
        m_wbLogged[static_cast<std::size_t>(adc)] = true;
        qCInfo(lcAnanP2T) << "ANAN: wideband packets arriving from ADC" << adc + 1 << "(first seq" << seq << ")";
    }
    const int ppf = std::max<int>(1, m_wb.packetsPerFrame);
    auto& buf = m_wbBuf[static_cast<std::size_t>(adc)];
    auto& next = m_wbNext[static_cast<std::size_t>(adc)];
    // A frame is ppf packets of contiguous samples; it starts at sequence 0
    // (Thetis network.c: the radio restarts the count every frame) -- or at
    // a multiple of ppf, should a board count on. A gap drops the frame.
    if (seq == 0 || seq % static_cast<std::uint32_t>(ppf) == 0) {
        buf.clear();
        buf.reserve(ppf * kWidebandSamplesPerPacket);
        next = static_cast<qint64>(seq);
    }
    if (next < 0 || static_cast<qint64>(seq) != next) {
        next = -1;
        return;
    }
    for (float v : m_wbScratch)
        buf.push_back(v);
    ++next;
    if (buf.size() >= ppf * kWidebandSamplesPerPacket) {
        ++m_diagWbFrames[static_cast<std::size_t>(adc)];
        emit widebandFrame(adc, buf);
        buf.clear();
        next = -1;
    }
}

void P2Transport::setOpenCollectorMatrix(const QByteArray& rx, const QByteArray& tx,
                                         const QByteArray& actions)
{
    OcMatrix m;
    for (int b = 0; b < kOcBandCount; ++b) {
        m.rx[b] = b < rx.size() ? std::uint8_t(rx.at(b) & 0x7f) : std::uint8_t{0};
        m.tx[b] = b < tx.size() ? std::uint8_t(tx.at(b) & 0x7f) : std::uint8_t{0};
    }
    for (int pin = 0; pin < kOcPinCount; ++pin) {
        const int a = pin < actions.size() ? int(std::uint8_t(actions.at(pin))) : 2;
        m.action[pin] = a == 0 ? OcTxAction::Mox : a == 1 ? OcTxAction::Tune : OcTxAction::MoxTune;
    }
    m_oc = m;
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setLna(bool on)
{
    if (on == m_lna)
        return;
    m_lna = on;
    qCInfo(lcAnanP2T) << "ANAN: LNA (Alex preamp bit)" << (on ? "ON" : "off");
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setTxAntenna(int antenna)
{
    antenna = std::clamp(antenna, 0, 2);
    if (antenna == m_txAnt)
        return;
    m_txAnt = antenna;
    qCInfo(lcAnanP2T).noquote() << QStringLiteral("ANAN: TX antenna ANT%1").arg(antenna + 1);
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setAdcAttenuation(int adc, int db)
{
    if (adc < 0 || adc > 1)
        return;
    // Build 724: up to 60 for the Hermes-Lite 2's LNA (the ANAN encoders clamp
    // to 31 themselves).
    db = std::clamp(db, 0, kHl2MaxAttenuationDb);
    if (m_adcAtt[static_cast<std::size_t>(adc)] == db)
        return;
    m_adcAtt[static_cast<std::size_t>(adc)] = db;
    if (m_run && m_configured) {
        sendHighPriority(true);
        if ((adc == 1 && !m_adc1FullAttOnTx) || (adc == 0 && !m_adc0FullAttOnTx))
            sendTransmitSpecific();   // builds 542, 561: its transmit value is this one
    }
}

void P2Transport::setLocalCodec(bool present)
{
    // Build 726: a Hermes-Lite 2 with an AK4951 board. Without one the EP2
    // audio slot is not audio on the HL2 (hl2/MetisProtocol.h, EADDR) and
    // stays zero (sendP1Ep2).
    m_hl2Codec = present;
}

void P2Transport::submitSpeakerAudio(const QByteArray& interleavedInt16)
{
    // 48 kHz stereo int16 from Hl2Backend's codec mixer (I/O thread). As
    // Thetis does (ChannelMaster obbuffs -> network.c), a 64-frame packet
    // leaves as soon as it is complete, from the speaker sender thread; the
    // audio is produced from the radio's own IQ stream, so the rate is the
    // radio's and the codec FIFO in the FPGA absorbs the bursts.
    const auto* s = reinterpret_cast<const std::int16_t*>(interleavedInt16.constData());
    const std::size_t n = static_cast<std::size_t>(interleavedInt16.size()) / sizeof(std::int16_t);
    ++m_diagSpkSubmits;
    for (std::size_t i = 0; i < n; ++i)
        m_diagSpkPeak = std::max(m_diagSpkPeak, std::abs(int(s[i])));
    {
        std::lock_guard<std::mutex> lock(m_spkMutex);
        m_spkQueue.insert(m_spkQueue.end(), s, s + (n & ~std::size_t{1}));
        while (m_spkQueue.size() > kSpkQueueMax)
            m_spkQueue.pop_front();
    }
    m_spkCv.notify_one();
}

void P2Transport::clearSpeakerAudio()
{
    std::lock_guard<std::mutex> lock(m_spkMutex);
    m_spkQueue.clear();
    m_spkPrimed = false;
}

qint64 P2Transport::sendNative(const std::uint8_t* data, std::size_t len, quint16 port)
{
    std::shared_lock<std::shared_mutex> guard(m_sendGuard);
    if (m_nativeFd < 0)
        return -1;
    const qint64 sent = nativeUdpSendTo(m_nativeFd, m_nativeIp, port, data, len);
    if (sent > 0) {
        m_nativeTxBytes.fetch_add(static_cast<quint64>(sent), std::memory_order_relaxed);
        m_nativeTxPackets.fetch_add(1, std::memory_order_relaxed);
    }
    return sent;
}

void P2Transport::speakerSenderLoop()
{
    // Speaker sender thread ("anan-spk"): time critical (MMCSS Pro Audio on
    // Windows). Waits for complete 64-frame packets and sends each at once.
    raiseCurrentThreadToTimeCritical();
    std::int16_t buf[2 * kCodecFramesPerPacket];
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_spkMutex);
            constexpr std::size_t kPkt = 2 * kCodecFramesPerPacket;
            // Protocol 1: the speaker audio rides in the EP2 packets (sendP1Ep2).
            m_spkCv.wait(lock, [this] {
                return m_spkQuit
                    || (!m_p1Atomic.load(std::memory_order_relaxed) && m_spkQueue.size() >= kPkt);
            });
            if (m_spkQuit)
                return;
            for (auto& v : buf) {
                v = m_spkQueue.front();
                m_spkQueue.pop_front();
            }
        }
        if (!m_sessionUp.load(std::memory_order_acquire))
            continue;
        // Build 617: a trim of the radio's speaker FIFO leaves out whole
        // silent packets only (see kSpkFifoTarget).
        if (m_spkTrimFrames.load(std::memory_order_acquire) >= kCodecFramesPerPacket) {
            bool silent = true;
            for (auto v : buf) {
                if (v > 2 || v < -2) {
                    silent = false;
                    break;
                }
            }
            if (silent) {
                m_spkTrimmedPkts.fetch_add(1, std::memory_order_relaxed);
                if (m_spkTrimFrames.fetch_sub(kCodecFramesPerPacket, std::memory_order_acq_rel)
                        - kCodecFramesPerPacket < kCodecFramesPerPacket) {
                    m_spkTrimFrames.store(0, std::memory_order_release);
                    m_spkTrimDoneMs.store(TxCoordinator::monotonicMs(), std::memory_order_relaxed);
                }
                continue;
            }
        }
        const auto pkt = buildSpeakerAudio(m_spkSeq++, std::span<const std::int16_t>(buf, std::size(buf)));
        if (sendNative(pkt.data(), pkt.size(), kSpeakerAudioPort) < 0)
            m_nativeSendFails.fetch_add(1, std::memory_order_relaxed);
        m_diagSpkPkts.fetch_add(1, std::memory_order_relaxed);
    }
}

void P2Transport::onSpeakerPacerTick()
{
    // ANAN-100D port: unused -- speaker packets leave from the speaker sender
    // thread as they are produced (Thetis behaviour); the timer never starts.
}

void P2Transport::setTxAttenuation(int db)
{
    db = std::clamp(db, 0, 31);
    if (db == m_txAttDb)
        return;
    m_txAttDb = db;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: RX attenuation while transmitting" << db << "dB";
    if (m_run && m_configured)
        sendTransmitSpecific();
}

void P2Transport::setMicBoost(bool on)
{
    if (on == m_micBoost)
        return;
    m_micBoost = on;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: mic boost" << (on ? "+20 dB" : "off");
    if (m_run && m_configured)
        sendTransmitSpecific();
}
void P2Transport::setMicControl(int bits)
{
    const auto b = static_cast<std::uint8_t>(bits & (kMicPttDisabled | kMicTipIsPtt | kMicBiasOn | kMicXlr));
    if (b == m_micControl)
        return;
    m_micControl = b;
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: mic PTT " << ((b & kMicPttDisabled) ? "off" : "on")
                                << ", PTT on " << ((b & kMicTipIsPtt) ? "tip" : "ring")
                                << ", bias " << ((b & kMicBiasOn) ? "on" : "off")
                                << ", input " << ((b & kMicXlr) ? "XLR" : "3.5 mm");
    if (m_run && m_configured)
        sendTransmitSpecific();
}

bool P2Transport::psBypassesBank(int adc) const
{
    // Build 423 (operator, 15.11: a 7000DLE on Protocol 1 -- "the relay that
    // takes the coupler's signal does not switch"). OUR ERROR in 396 / 397:
    // the bypass followed the PS feedback ADC's bank only. Thetis
    // (console.cs setBPF1ForOrionIISaturn, both protocols) bypasses BPF1 --
    // the ADC1 bank, SetAlexHPFBits(0x20) -- on MOX with PureSignal on,
    // whichever ADC the feedback is read from; the internal coupler comes in
    // through that bank. ADC2's bank is bypassed as well when the feedback is
    // read on ADC2 (ours, as before).
    // Build 482 (operator, 17.11, a 7000DLE on Protocol 1: "it must switch
    // on transmit even with PureSignal off"). The input relays were already
    // sent on every transmission (build 438), but this bypass was not: with
    // PureSignal off BPF1 stayed on the receive filter, and on these boards
    // the chosen feedback input (coupler, EXT1, RX bypass) comes in through
    // the bypassed BPF1 -- so nothing appeared to switch. BPF1 is bypassed on
    // every transmission now; ADC2's bank still only for PureSignal's
    // feedback on ADC2.
    // Build 502 (operator, 18.11: "always on transmit does not suit
    // everybody"): BPF1 is bypassed with PureSignal on (Thetis) or when
    // "Bypass BPF When TX" is set. A board without the ORION MkII banks has
    // no PureSignal bypass (Thetis), but the option still sends its bank's
    // bypass on transmit -- the protocol's bit, whatever the board does
    // with it.
    if (!m_mox)
        return false;
    if (m_profile.alexScheme != AlexScheme::Orion2BandPass)
        return adc == 0 && m_txBypassBpf;
    // Build 505 (operator: "what was done yesterday with the relay was right,
    // it only has to be switchable"): yesterday's function -- BPF1 bypassed
    // on every transmission (482) together with the feedback input (438) --
    // is "PS Coupler Always when TX"; "Bypass BPF When TX" is the bypass
    // alone.
    // Build 506: "Bypass BPF2 When TX" -- ADC2's bank on every transmission
    // (else, as before, only for PureSignal's feedback on ADC2).
    return adc == 0 ? (m_psEnabled || m_txBypassBpf || m_psCouplerOnTx)
                    : ((m_psEnabled && m_psAdc == adc) || (adc == 1 && m_txBypassBpf2));
}

void P2Transport::setTxBypassBpf(bool on)
{
    if (on == m_txBypassBpf)
        return;
    m_txBypassBpf = on;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: Bypass BPF1 When TX" << (on ? "on" : "off");
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setTxBypassBpf2(bool on)
{
    if (on == m_txBypassBpf2)
        return;
    m_txBypassBpf2 = on;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: Bypass BPF2 When TX" << (on ? "on" : "off");
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setWbBypass(int adcMask)
{
    adcMask &= 3;
    if (adcMask == m_wbBypassMask)
        return;
    m_wbBypassMask = adcMask;
    // Protocol 1 takes it from p1State() in the C&C rotation (build 685).
    qCInfo(lcAnanP2T) << "ANAN transport: Wideband BPF Bypass"
                      << (adcMask == 0 ? QStringLiteral("off")
                                       : QStringLiteral("ADC%1").arg(adcMask == 2 ? 2 : 1));
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setPsCouplerOnTx(bool on)
{
    if (on == m_psCouplerOnTx)
        return;
    m_psCouplerOnTx = on;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: PS Coupler Always when TX" << (on ? "on" : "off");
    if (m_run && m_configured)
        sendHighPriority(true);
}

// ANAN fork build 542 (operator, 20.11: "Full ATT RX2 when TX -- 31 dB into
// the protocol, there is a byte for it; on by default"). Protocol 2: byte 58
// of the Transmitter Specific packet (ADC1's step attenuator while
// transmitting). Protocol 1: C0 0x16 C1 while MOX (P1State::txAdc1AttDb).
// Until now that byte followed the general transmit attenuator
// (AnanTxAttenuationDb, 31 by default); it is this option's now: 31 dB when
// on, ADC1's receive attenuation when off. PureSignal feedback on ADC1 keeps
// its own value in both cases.
// ANAN fork build 561 (operator, 21.11: "exactly the same check box for RX1,
// the protocol has the same for the first receiver"). ADC0 while
// transmitting -- Protocol 2: byte 59 of the Transmitter Specific packet;
// Protocol 1: C0 0x1C C3 and ADC0's own byte (P1State::txAdc0AttDb). On (the
// default): the transmit attenuator, AnanTxAttenuationDb -- 31 dB unless the
// key was edited, which is what was sent until now. Off: ADC0's receive
// attenuation. PureSignal feedback on ADC0 keeps its own value in both cases.
void P2Transport::setAdc0FullAttOnTx(bool on)
{
    if (on == m_adc0FullAttOnTx)
        return;
    m_adc0FullAttOnTx = on;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: ADC1 (RX1) full attenuation on TX" << (on ? "on" : "off");
    if (m_run && m_configured) {
        sendTransmitSpecific();
        sendHighPriority(true);
    }
}

void P2Transport::setAdc1FullAttOnTx(bool on)
{
    if (on == m_adc1FullAttOnTx)
        return;
    m_adc1FullAttOnTx = on;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: ADC2 (RX2) full attenuation on TX" << (on ? "on" : "off");
    if (m_run && m_configured) {
        sendTransmitSpecific();
        sendHighPriority(true);
    }
}

void P2Transport::setAdc1GroundOnTx(bool on)
{
    if (on == m_adc1GndOnTx)
        return;
    m_adc1GndOnTx = on;
    qCInfo(lcAnanP2T) << "ANAN P2 transport: ADC2 input grounded on TX" << (on ? "on" : "off");
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setForcedBoardId(int reportedBoardId, int forcedBoardId)
{
    if (reportedBoardId <= 0 || reportedBoardId >= int(m_forcedBoardFor.size()))
        return;
    const int was = m_forcedBoardFor[std::size_t(reportedBoardId)].exchange(forcedBoardId, std::memory_order_relaxed);
    if (was != forcedBoardId)
        qCInfo(lcAnanP2T) << "ANAN transport: board" << reportedBoardId << "forcedly assigned as board"
                          << forcedBoardId << "(0 = itself; applies from the next connection)";
}

BoardProfile P2Transport::forcedProfileOr(const BoardProfile& reported) const
{
    const int forced = reported.boardId < m_forcedBoardFor.size()
        ? m_forcedBoardFor[reported.boardId].load(std::memory_order_relaxed) : 0;
    if (forced <= 0 || forced == reported.boardId)
        return reported;
    // Build 723: a Hermes-Lite 2 is never driven as an ANAN, nor an ANAN as it.
    if (reported.boardId == kBoardIdHermesLite || forced == kBoardIdHermesLite)
        return reported;
    const auto profile = boardProfileFor(static_cast<std::uint8_t>(forced));
    if (!profile)
        return reported;
    // Protocol 1 firmware exists for these boards only (p1DdcPlanFor).
    if (m_p1 && forced != kBoardIdHermes && forced != kBoardIdHermesII && forced != kBoardIdAngelia
        && forced != kBoardIdOrion && forced != kBoardIdOrion2) {
        qCWarning(lcAnanP2T) << "ANAN transport: forcedly assigned" << profile->modelName
                             << "cannot run over Protocol 1 -- driving the reported" << reported.modelName;
        return reported;
    }
    qCWarning(lcAnanP2T) << "ANAN transport: the radio reports" << reported.modelName << "(board"
                         << int(reported.boardId) << "); driven as" << profile->modelName << "(board"
                         << forced << ") -- forcedly assigned by the operator";
    return *profile;
}

// ---- ANAN fork build 726: Hermes-Lite 2 settings (Quisk hermes/
// quisk_hardware.py; the gateware notes in hl2/MetisProtocol.h) ----

void P2Transport::setCl1RefClock(bool externalRef)
{
    // The VersaClock is reprogrammed by 24 I2C1 writes (versaClockCl1Banks,
    // deskHPSDR / piHPSDR verbatim); the board boots with CL1 off.
    if (externalRef == m_hl2Cl1)
        return;
    m_hl2Cl1 = externalRef;
    if (!isHl2Session())
        return;
    for (const hl2::Cc& cc : hl2::versaClockCl1Banks(externalRef))
        m_hl2Once.push_back(cc);
    qCInfo(lcAnanP2T) << "HL2: CL1 external 10 MHz reference" << (externalRef ? "on" : "off");
}

void P2Transport::setAtuTuneRequest(bool request)
{
    m_hl2AtuTune = request;   // 0x09[20] in the rotation (p1CcHl2)
}

void P2Transport::setIoBoardTxFrequencyHz(quint64 hz)
{
    // N2ADR IO board (I2C2 chip 0x1D): the TX frequency in registers 0..4,
    // the LSB last -- it commits the five (Quisk IOBoard.HeartBeat). Written
    // whether or not a board answers: an absent chip ignores it.
    if (!isHl2Session() || hz == 0)
        return;
    for (const hl2::Cc& cc : hl2::ccIoBoardTxFrequency(hz))
        m_hl2Once.push_back(cc);
}

void P2Transport::setHl2Options(const QVariantMap& in)
{
    const Hl2Opts was = m_hl2;
    auto b = [&in](const char* k, bool cur) {
        const auto it = in.constFind(QLatin1String(k));
        return it == in.constEnd() ? cur : it->toBool();
    };
    auto i = [&in](const char* k, int cur, int lo, int hi) {
        const auto it = in.constFind(QLatin1String(k));
        return it == in.constEnd() ? cur : std::clamp(it->toInt(), lo, hi);
    };
    m_hl2.powerAmp = b("powerAmp", m_hl2.powerAmp);
    m_hl2.lowPwrTrDisable = b("lowPwrTrDisable", m_hl2.lowPwrTrDisable);
    m_hl2.txLatencyMs = i("txLatencyMs", m_hl2.txLatencyMs, 0, 127);
    m_hl2.pttHangMs = i("pttHangMs", m_hl2.pttHangMs, 0, 31);
    m_hl2.txLnaDb = i("txLnaDb", m_hl2.txLnaDb, -12, 48);
    m_hl2.disableSync = b("disableSync", m_hl2.disableSync);
    m_hl2.disableWatchdog = b("disableWatchdog", m_hl2.disableWatchdog);
    m_hl2.resetOnDisconnect = b("resetOnDisconnect", m_hl2.resetOnDisconnect);
    m_hl2.iobRxIn = i("iobRxIn", m_hl2.iobRxIn, 0, 2);
    if (!isHl2Session())
        return;
    // Quisk ImmediateChange: the watchdog (addr 0x39 C1 0x09 off / 0x08 on)
    // and the IO board's receive input (register 11) are one-shot writes.
    if (m_hl2.disableWatchdog != was.disableWatchdog)
        m_hl2Once.push_back(hl2::Cc{0x72, std::uint8_t(m_hl2.disableWatchdog ? 0x09 : 0x08), 0, 0, 0});
    if (m_hl2.iobRxIn != was.iobRxIn)
        m_hl2Once.push_back(hl2::ccI2c2Write(hl2::kIoBoardI2cAddr, 11, std::uint8_t(m_hl2.iobRxIn)));
}

// At the start of a Hermes-Lite 2 session: what Quisk sends once sound has
// started (HeartBeat: the watchdog, the IO board's receive input) and the CL1
// programming when it is on (the board boots with it off).
void P2Transport::queueHl2SessionSetup()
{
    m_hl2Once.clear();
    m_hl2OnceGap = false;
    m_hl2IobRxCode = 0xFF;   // build 727: sent again
    m_hl2Once.push_back(hl2::Cc{0x72, std::uint8_t(m_hl2.disableWatchdog ? 0x09 : 0x08), 0, 0, 0});
    m_hl2Once.push_back(hl2::ccI2c2Write(hl2::kIoBoardI2cAddr, 11, std::uint8_t(m_hl2.iobRxIn)));
    if (m_hl2Cl1)
        for (const hl2::Cc& cc : hl2::versaClockCl1Banks(true))
            m_hl2Once.push_back(cc);
    qCInfo(lcAnanP2T).nospace()
        << "HL2 settings: PA " << (m_hl2.powerAmp ? "on" : "off") << ", T/R off at low power "
        << m_hl2.lowPwrTrDisable << ", TX latency " << m_hl2.txLatencyMs << " ms, PTT hang "
        << m_hl2.pttHangMs << " ms, TX LNA " << m_hl2.txLnaDb << " dB, sync off " << m_hl2.disableSync
        << ", watchdog off " << m_hl2.disableWatchdog << ", reset on disconnect "
        << m_hl2.resetOnDisconnect << ", IO board RX input " << m_hl2.iobRxIn << ", codec "
        << m_hl2Codec << ", CL1 " << m_hl2Cl1;
}

void P2Transport::requestPipelineReset()
{
    m_expectedSeq.fill(std::nullopt);
    for (auto& q : m_pending)
        q.clear();
}

bool P2Transport::requestRegister(int addr, quint32, bool)
{
    // HL2 I2C/register access has no Protocol 2 counterpart.
    QMetaObject::invokeMethod(this, [this, addr] { emit controlRequestFailed(addr, true); },
                              Qt::QueuedConnection);
    return false;
}

void P2Transport::requestIoBoardStatus()
{
    QMetaObject::invokeMethod(this, [this] {
        emit ioBoardStatusFailed(QStringLiteral("no IO board on ANAN"));
    }, Qt::QueuedConnection);
}

void P2Transport::setBandscopeEnabled(bool) {}

void P2Transport::requestBandscopeFrame()
{
    QMetaObject::invokeMethod(this, [this] {
        emit bandscopeFrameFailed(QStringLiteral("wideband view not implemented for ANAN yet"));
    }, Qt::QueuedConnection);
}

// ---- transmit -----------------------------------------------------------

void P2Transport::sendTransmitSpecific()
{
    if (!m_sock || m_p1)
        return;
    // Build 542: ADC1 (RX2) -- 31 dB with "Full ATT RX2 when TX", otherwise
    // its receive attenuation (nothing is added on transmit).
    const auto pkt = buildTransmitSpecific(m_txSpecSeq++, m_micBoost, effectiveCwKeyer(), m_txAttDb,
                                           m_psEnabled ? m_psAttDb : -1, m_psAdc, m_micControl,
                                           m_adc1FullAttOnTx ? kMaxStepAttenuationDb : m_adcAtt[1],
                                           m_adc0FullAttOnTx ? m_txAttDb : m_adcAtt[0]);   // build 561
    sendDatagram(*m_sock, pkt, m_txHost, kTransmitSpecificPort, m_counters.txBytes, m_counters.txPackets);
    publishP2AttNow();   // build 751
}

// ANAN fork build 751 (operator, 25.11: "any change of the attenuators must
// show there at once" -- the RZ3QS branch's ATT indicator, build 750): the
// attenuation each ADC has now, published by the packets that carry it.
// Protocol 2: receiving, the receive attenuators; transmitting, what the
// Transmit Specific packet gives each ADC -- PureSignal's on the feedback
// ADC, else 31 dB / the transmit attenuator with "Full ATT when TX", else the
// receive value. Protocol 1: from the C&C state itself (p1State).
void P2Transport::publishAttNow(int adc0Db, int adc1Db) const
{
    auto& ui = AnanUiState::instance();
    const int old0 = ui.adcAttNowDb[0].exchange(adc0Db, std::memory_order_relaxed);
    const int old1 = ui.adcAttNowDb[1].exchange(adc1Db, std::memory_order_relaxed);
    if (old0 != adc0Db || old1 != adc1Db)
        ui.attNowSerial.fetch_add(1, std::memory_order_relaxed);
}

void P2Transport::publishP2AttNow() const
{
    const int rx0 = std::clamp(m_adcAtt[0], 0, kMaxStepAttenuationDb);
    const int rx1 = std::clamp(m_adcAtt[1], 0, kMaxStepAttenuationDb);
    if (!m_mox) {
        publishAttNow(rx0, rx1);
        return;
    }
    int a0 = m_adc0FullAttOnTx ? m_txAttDb : rx0;
    int a1 = m_adc1FullAttOnTx ? kMaxStepAttenuationDb : rx1;
    if (m_psEnabled) {
        if (m_psAdc == 1)
            a1 = m_psAttDb;
        else
            a0 = m_psAttDb;
    }
    publishAttNow(a0, a1);
}

void P2Transport::setMox(bool keyed, const TxCoordinator::Operation& operation)
{
    // Build 850: no CWX line left over an unkey -- with break-in the radio's
    // keyer would key the transmitter by itself.
    if (!keyed)
        clearCwxLines();
    if (!TxCoordinator::Command{operation, keyed}.permitsDispatch(TxCoordinator::monotonicMs()))
        return;
    if (keyed && !transmitEnabled())
        keyed = false;
    std::unique_lock<std::mutex> txLock(m_txMutex);
    // Build 632: the RF tail of an unkey -- a key continues the over (MOX
    // never dropped), a second unkey changes nothing.
    if (m_unkeyTail) {
        if (keyed) {
            m_unkeyTail = false;
            ++m_unkeyTailGen;
            qCInfo(lcAnanP2T) << "ANAN P2 transport: keyed again within the RF Delay tail -- the over goes on";
        }
        return;
    }
    if (keyed == m_mox)
        return;
    const int tailMs = std::max(0, m_rfDelayMs.load(std::memory_order_relaxed));
    if (!keyed && !m_cwMode && !m_cwKeyer.enabled && tailMs > 0 && m_run && m_configured) {
        m_unkeyTail = true;
        m_unkeyFadeLeft = kUnkeyFadeSamples;
        const quint32 gen = ++m_unkeyTailGen;
        qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: unkey -- RF down in 2 ms, MOX off in "
                                    << tailMs << " ms (RF Delay)";
        txLock.unlock();
        QTimer::singleShot(tailMs, Qt::PreciseTimer, this, [this, gen] {
            std::unique_lock<std::mutex> lock(m_txMutex);
            if (!m_unkeyTail || gen != m_unkeyTailGen)
                return;
            m_unkeyTail = false;
            if (!m_mox)
                return;
            applyMox(false, lock);
        });
        return;
    }
    applyMox(keyed, txLock);
}

void P2Transport::applyMox(bool keyed, std::unique_lock<std::mutex>& txLock)
{
    m_mox = keyed;
    m_moxAtomic.store(keyed, std::memory_order_relaxed);
    if (keyed && m_tlPressMs >= 0 && m_tlMoxMs < 0)
        m_tlMoxMs = m_tlClock.elapsed();
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: MOX " << (keyed ? "ON" : "off")
                                << " at " << (m_txHz ? m_txHz : m_rxHz[0]) << " Hz, drive "
                                << m_drive;
    if (keyed) {
        // Build 466: a receiver on DDC0 keeps sending for a moment after the
        // DDC-Specific packet that hands it to PureSignal.
        bool rxOnDdc0 = false;   // (Protocol 1: on the feedback DDC, build 468)
        for (int i = 0; i < m_numRx; ++i) {
            if (m_p1)
                rxOnDdc0 = rxOnDdc0 || (m_p1Plan.psFbDdc >= 0 && m_p1Plan.nddc > 2
                                        && m_p1Plan.rxDdc[static_cast<std::size_t>(i)] == m_p1Plan.psFbDdc);
            else if (ddcWrap() > 0)
                rxOnDdc0 = rxOnDdc0 || hwDdcOfRx(i) == 0;
        }
        m_psAcceptFromMs.store(TxCoordinator::monotonicMs() + (rxOnDdc0 ? kPsReleaseMs : 0),
                               std::memory_order_relaxed);
        m_hwDucPk = m_hwFbPk = 0.0;
        m_hwDucLast.store(0.0, std::memory_order_relaxed);
        m_hwDucN = m_hwFbN = 0;
        m_scopeFbPk = m_scopeFbGain = 0.0;
        m_scopeFromFb.store(false, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(m_ducSnapMutex);
            m_ducSnapFill = 0;
        }
        beginTxStream();
        if (m_p1) {
            m_p1TxDec.reset();
            m_p1CcLogPending.store(true, std::memory_order_relaxed);
        }
    } else {
        m_txTimer->stop();
        m_txQueue.clear();
        m_cwEnv = 0.0;
        logTxOver();
        if (m_txUnderflows)
            qCInfo(lcAnanP2T) << "ANAN P2 transport: TX underflow packets this over:" << m_txUnderflows;
        if (m_psEnabled)
            qCInfo(lcAnanP2T) << "ANAN P2 transport: PureSignal feedback packets (total):" << m_psPackets;
    }
    // Build 466: RX3 / RX4 stay held a little after MOX, until PureSignal's
    // last synchronised packets have gone by.
    if (!keyed)
        m_psReleaseUntilMs.store(TxCoordinator::monotonicMs() + kPsReleaseMs,
                                 std::memory_order_relaxed);
    txLock.unlock();
    if (m_run && m_configured) {
        // PureSignal: DDC0/DDC1 become the feedback pair only while MOX.
        if (m_psEnabled)
            sendDdcSpecific();
        sendHighPriority(true);
    }
}

void P2Transport::unkeyOnLinkLoss()
{
    std::unique_lock<std::mutex> txLock(m_txMutex);
    const bool wasKeyed = m_mox || m_cwDown;
    m_cwDown = false;
    if (!m_mox) {
        txLock.unlock();
        if (wasKeyed && m_run && m_configured)
            sendHighPriority(true);
        return;
    }
    m_mox = false;
    m_moxAtomic.store(false, std::memory_order_relaxed);
    m_unkeyTail = false;   // build 632
    ++m_unkeyTailGen;
    m_psReleaseUntilMs.store(TxCoordinator::monotonicMs() + kPsReleaseMs,
                             std::memory_order_relaxed);   // build 466
    qCWarning(lcAnanP2T).nospace() << "ANAN P2 transport: MOX off -- the link to the radio was lost while "
                                      "transmitting; it does not come back on by itself";
    m_txTimer->stop();
    m_txQueue.clear();
    m_cwEnv = 0.0;
    logTxOver();
    txLock.unlock();
    if (m_run && m_configured) {
        if (m_psEnabled)
            sendDdcSpecific();
        sendHighPriority(true);
    }
}

void P2Transport::logTxOver()
{
    // One line per transmission: how loud the DUC IQ was, relative to full
    // scale (|IQ| = 1, a full-scale TUNE carrier = 0 dBFS). Mean power is
    // what an ammeter / average wattmeter follows; peak is PEP.
    if (m_ovCount == 0)
        return;
    auto db = [](double p) { return 10.0 * std::log10(std::max(p, 1e-20)); };
    const double mean = m_ovSumSq / double(m_ovCount);
    qCInfo(lcAnanP2T).nospace()
        << "ANAN P2 transport: TX over " << QString::number(m_ovCount / double(kDucSampleRateHz), 'f', 1)
        << " s: IQ peak " << QString::number(db(m_ovPeak), 'f', 1)
        << " dBFS, mean " << QString::number(db(mean), 'f', 1)
        << " dBFS, loudest 100 ms " << QString::number(db(m_ovWinMax), 'f', 1)
        << " dBFS, drive " << m_drive << "/255"
        << (m_toneAmp > 0.0 ? " (tone)" : m_cwMode ? " (host CW)" : "");
    QString tr;
    for (double v : m_ovTrace)
        tr += QString::number(db(v), 'f', 0) + QLatin1Char(' ');
    qCInfo(lcAnanP2T).nospace() << "ANAN P2 transport: TX IQ mean per 250 ms [" << tr << "] dBFS";
    m_ovTrace.clear();
    m_ovTrSum = 0.0;
    m_ovTrN = 0;
    m_ovCount = 0;
}

void P2Transport::setCwMox(bool keyed, const TxCoordinator::Operation& operation)
{
    setMox(keyed, operation);
}

void P2Transport::beginTxStream()
{
    m_txPrimed = false;
    m_txUnderflows = 0;
    m_hostZeroRun = 0;
    m_hostZeroRuns = m_hostZeroRunsLogged = 0;
    m_txPacketsSent = 0;
    m_txSamplesOut = 0;
    m_txRise = 0;
    // Nothing from before this key: IQ the modulator finished after the last
    // unkey (queued behind it) would otherwise open this over.
    m_txQueue.clear();
    m_txScope.reset();
    m_ovPeak = m_ovSumSq = m_ovWinSumSq = m_ovWinMax = 0.0;
    m_ovTrace.clear();
    m_ovTrSum = 0.0;
    m_ovTrN = 0;
    m_ovCount = 0;
    m_ovWinCount = 0;
    m_txUnderflowsLogged = 0;
    // The radio's clock (txDeviceTick) paces from here; the lead goes out at
    // once. The 2 ms host timer only stands in without a mic stream.
    m_txDevSamples = 0;
    m_leadAdj.store(0, std::memory_order_relaxed);
    m_txFallbackCredit = 0;
    m_txFallbackActive = false;
    m_micProcessed = m_micArrived.load(std::memory_order_relaxed);   // no backlog from before the key
    m_txFallbackClock.start();
    m_txFallbackNs = 0;
    txPumpLocked();
    m_txTimer->start();
}

static_assert(kDucSampleRateHz == anan::TxScope::kSampleRateHz,
              "TxScope's frequency axis assumes the DUC rate");

void P2Transport::fillTxPacket(std::complex<float>* out, int n)
{
    fillTxPacketSource(out, n);
    // Build 632: the RF tail of a voice / TUNE unkey -- a 2 ms linear fall,
    // then zeros until MOX drops.
    if (!m_unkeyTail)
        return;
    for (int i = 0; i < n; ++i) {
        if (m_unkeyFadeLeft > 0) {
            out[i] *= static_cast<float>(m_unkeyFadeLeft) / static_cast<float>(kUnkeyFadeSamples);
            --m_unkeyFadeLeft;
        } else {
            out[i] = {};
        }
    }
}

void P2Transport::fillTxPacketSource(std::complex<float>* out, int n)
{
    constexpr double fs = kDucSampleRateHz;
    if (m_toneAmp > 0.0) {
        // ANAN fork build 836 (operator, 27.11: "is the output delay applied
        // to TUNE?"): it was not -- the TUNE carrier (and the test tone) went
        // out from the first packet after MOX, while the T/R relays were
        // still moving. Now as voice: silence for the RF Delay after MOX,
        // then a 2 ms rise.
        const std::uint64_t delaySamples = static_cast<std::uint64_t>(
            std::max(0, m_rfDelayMs.load(std::memory_order_relaxed))) * kDucSampleRateHz / 1000;
        if (m_txSamplesOut < delaySamples) {
            std::fill(out, out + n, std::complex<float>{});
            m_txSamplesOut += static_cast<std::uint64_t>(n);
            m_txRise = 0;
            return;
        }
        constexpr int kToneRise = kDucSampleRateHz / 500;   // 2 ms
        const double step = 2.0 * std::numbers::pi * m_toneHz / fs;
        for (int i = 0; i < n; ++i) {
            double a = m_toneAmp;
            if (m_txRise < kToneRise) {
                a *= 0.5 - 0.5 * std::cos(std::numbers::pi * double(m_txRise) / double(kToneRise));
                ++m_txRise;
            }
            out[i] = {static_cast<float>(a * std::cos(m_tonePhase)),
                      static_cast<float>(a * std::sin(m_tonePhase))};
            m_tonePhase += step;
            if (m_tonePhase > 2.0 * std::numbers::pi)
                m_tonePhase -= 2.0 * std::numbers::pi;
        }
        return;
    }
    if (m_cwMode) {
        // Host CW: a carrier at the DUC frequency, 5 ms raised ramps.
        const double rampStep = 1.0 / (0.005 * fs);
        for (int i = 0; i < n; ++i) {
            m_cwEnv += m_cwDown ? rampStep : -rampStep;
            m_cwEnv = std::clamp(m_cwEnv, 0.0, 1.0);
            const double shaped = 0.5 - 0.5 * std::cos(std::numbers::pi * m_cwEnv);
            out[i] = {static_cast<float>(shaped), 0.0f};
        }
        return;
    }
    // ANAN-100D port (operator, 31.10): voice starts when the transmitter is
    // ready -- RF Delay (byte 13, TX applet) after MOX -- and no later. Until
    // then silence goes out, and the queue keeps only the newest prime's
    // worth of modulation, so the first audio after the delay is current
    // (no backlog, no added latency); then a 2 ms rise.
    {
        const std::uint64_t delaySamples = static_cast<std::uint64_t>(
            std::max(0, m_rfDelayMs.load(std::memory_order_relaxed))) * kDucSampleRateHz / 1000;
        if (m_txSamplesOut < delaySamples) {
            while (m_txQueue.size() > static_cast<std::size_t>(m_txPrime.load(std::memory_order_relaxed)))
                m_txQueue.pop_front();
            std::fill(out, out + n, std::complex<float>{});
            m_txSamplesOut += static_cast<std::uint64_t>(n);
            m_txRise = 0;
            return;
        }
    }
    if (!m_txPrimed && m_txQueue.size() >= static_cast<std::size_t>(m_txPrime.load(std::memory_order_relaxed)))
        m_txPrimed = true;
    if (!m_txPrimed || m_txQueue.size() < static_cast<std::size_t>(n)) {
        if (m_txPrimed) {
            // Counted here (time-critical thread); logged by the I/O thread.
            ++m_txUnderflows;
            m_txPrimed = false;   // re-prime before using the queue again
        }
        std::fill(out, out + n, std::complex<float>{});
        return;
    }
    constexpr int kRise = kDucSampleRateHz / 500;   // 2 ms
    for (int i = 0; i < n; ++i) {
        out[i] = m_txQueue.front();
        m_txQueue.pop_front();
        if (m_txRise < kRise) {
            const float w = 0.5f - 0.5f * std::cos(3.14159265f * float(m_txRise) / float(kRise));
            out[i] *= w;
            ++m_txRise;
        }
    }
    m_txSamplesOut += static_cast<std::uint64_t>(n);
}

void P2Transport::onTxPacerTick()
{
    // I/O thread, every 2 ms while MOX. Normally only bookkeeping: the radio's
    // microphone stream drives the DUC (txDeviceTick). Without it for 30 ms
    // (a radio that sends none, or the transmit thread held up) the host's
    // clock advances the DUC instead, so the transmitter never starves.
    quint64 underflows = 0;
    {
        std::lock_guard<std::mutex> lock(m_txMutex);
        if (!m_mox)
            return;
        const qint64 ns = m_txFallbackClock.nsecsElapsed();
        const qint64 sinceDev = steadyMs() - m_lastDevTickMs.load(std::memory_order_relaxed);
        if (sinceDev > 30) {
            // On the first stand-in tick, the whole time since the last mic
            // packet (bounded by this over); later ticks, the tick itself.
            const qint64 fromNs = m_txFallbackActive
                ? m_txFallbackNs
                : std::max<qint64>(ns - sinceDev * 1000000LL, 0);
            m_txFallbackActive = true;
            const quint64 add = static_cast<quint64>(double(ns - fromNs) * 1e-9 * kDucSampleRateHz);
            m_txDevSamples += add;
            // Time the host has stood in for: mic packets that were only late
            // (not absent) arrive in a burst afterwards and must not count it
            // twice (a fake-radio test drained the queue that way). Capped,
            // so a radio without a mic stream does not bank a long silence.
            m_txFallbackCredit = std::min<quint64>(m_txFallbackCredit + add, kDucSampleRateHz / 20);
            txPumpLocked();
        } else {
            m_txFallbackActive = false;
        }
        m_txFallbackNs = ns;
        if (m_txUnderflows != m_txUnderflowsLogged) {
            underflows = m_txUnderflows;
            m_txUnderflowsLogged = m_txUnderflows;
        }
        if (m_hostZeroRuns != m_hostZeroRunsLogged) {
            qCInfo(lcAnanP2T) << "ANAN P2 transport: the HOST sent a silent run in the DUC stream:"
                              << m_hostZeroLastLen << "samples below -80 dBFS, ending in DUC packet"
                              << m_hostZeroLastSeq << ", queue" << m_hostZeroLastQueue << "samples; runs this over:"
                              << m_hostZeroRuns;
            m_hostZeroRunsLogged = m_hostZeroRuns;
        }
        if (m_txLateTicks != m_txLateLogged) {
            qCInfo(lcAnanP2T) << "ANAN P2 transport: transmit thread behind the radio --" << m_txLateTicks - m_txLateLogged
                              << "mic packet(s) of air time skipped, not sent late";
            m_txLateLogged = m_txLateTicks;
        }
    }
    if (underflows)
        qCInfo(lcAnanP2T) << "ANAN P2 transport: TX underflow" << underflows
                          << "-- the modulator was late; DUC zeros until re-primed";
}

void P2Transport::txDeviceTick(int ducSamples)
{
    std::lock_guard<std::mutex> lock(m_txMutex);
    m_lastDevTickMs.store(steadyMs(), std::memory_order_relaxed);
    const quint64 packets = static_cast<quint64>(std::max(ducSamples, 0)) / 256;
    m_micProcessed += packets;
    if (!m_mox || ducSamples <= 0)
        return;
    quint64 add = static_cast<quint64>(ducSamples);
    const quint64 absorbed = std::min(add, m_txFallbackCredit);   // already stood in for
    m_txFallbackCredit -= absorbed;
    add -= absorbed;
    if (add == 0)
        return;
    // The transmit thread is far behind the radio (more than 16 mic packets,
    // 21 ms, still waiting -- beyond what the radio's 4096-sample DUC FIFO
    // can take as a catch-up burst): this time has passed on the air. Its IQ
    // is not sent late; time and queue move on, and the lead is
    // re-established once the backlog is down. Smaller backlogs are sent as
    // a burst (they fit the FIFO; dropping them would cost an underflow).
    const quint64 arrived = m_micArrived.load(std::memory_order_relaxed);
    if (arrived > m_micProcessed + 16) {
        m_txDevSamples += add;
        for (quint64 k = 0; k < add && !m_txQueue.empty(); ++k)
            m_txQueue.pop_front();
        const quint64 due = m_txDevSamples / kDucSamplesPerPacket + kTxLeadPackets;
        if (m_txPacketsSent + kTxLeadPackets < due)
            m_txPacketsSent = due - kTxLeadPackets;
        ++m_txLateTicks;
        return;
    }
    m_txDevSamples += add;
    txPumpLocked();
}

// ANAN fork build 348 (operator, 13.11: "can we drive the scale fully?").
// No: the radio's own transmit chain overflows just under full scale. In the
// standard openHPSDR firmware (read in Odyssey-3 2.0 ANP: Odyssey.v,
// CicInterpM5.v, cpl_cordic.v, and Quartus' own warning that
// out_data_I[23..22] have no fanouts) the interpolator's gain, 625/131072,
// takes a full-scale sample to 0.6104 of its output, the CORDIC's gain is
// 1.64676, and the product, 1.0051, goes into a 22-bit word WITHOUT
// saturation. Above 0.9949 of full scale the peaks of the carrier wrap to the
// opposite rail: at exactly 1.0, 6.4 % of the DAC samples, the fundamental
// 2.5 dB down and nearly as much power beside it (bit-exact model, journal
// section 313). The firmware cannot be changed -- thousands of radios run it
// -- so the ceiling is here, on the samples as they go to the wire, both
// protocols: the envelope is held to 1 (a radial limit; only a fault would
// reach it, the ALC holds the envelope) and scaled by 0.99 (-0.087 dB).
// The TX panadapter and its drive line see the samples before this: there,
// 0 dB is the most the program sends.
namespace {
constexpr float kTxWireCeiling = 0.99f;
void limitTxDrive(std::complex<float>* buf, int n) noexcept
{
    for (int i = 0; i < n; ++i) {
        float re = buf[i].real(), im = buf[i].imag();
        const float m2 = re * re + im * im;
        const float g = m2 > 1.0f ? kTxWireCeiling / std::sqrt(m2) : kTxWireCeiling;
        buf[i] = {re * g, im * g};
    }
}
}   // namespace

// Build 425 (operator, 15.11: "the peak line must measure before the
// correction"). The TXA CFIR (the DUC CIC's pre-compensation, Thetis on
// every Protocol 2 radio, ours on both protocols) has a passband gain of
// 1 / droop(20 kHz)^5 = 0.9143 (CIC R 640, 5 stages at 192 kHz; the same
// figure as AnanPureSignal's cfirPassbandGain()). The TX drive peak line
// shows the modulator's samples on the scale before it: the most the ALC
// lets through reads 0 dB. TUNE's tone and the host CW are made here, after
// the CFIR: they are shown as they are.
// Build 436: superseded -- the CFIR's gain is made up in Hl2TxDsp, so the
// wire samples themselves are on that scale.

void P2Transport::accountTxSamples(const std::complex<float>* buf, int n)
{
    // Live = primed and 25 ms past the RF Delay (the start and the 2 ms
    // rise are silent by design; an underflow is counted on its own).
    const std::uint64_t liveFrom = static_cast<std::uint64_t>(
        std::max(0, m_rfDelayMs.load(std::memory_order_relaxed))) * kDucSampleRateHz / 1000
        + kDucSampleRateHz / 40;
    const bool live = m_txPrimed && m_txSamplesOut > liveFrom && !m_cwMode && m_toneAmp <= 0.0
        && !m_unkeyTail;   // build 632: the unkey tail is silent by design
    for (int k = 0; k < n; ++k) {
        const auto& v = buf[k];
        const double p = double(v.real()) * v.real() + double(v.imag()) * v.imag();
        if (live && p < 1e-8) {   // |x| < 1e-4 of full scale (-80 dBFS)
            ++m_hostZeroRun;
        } else {
            if (m_hostZeroRun >= 64) {
                ++m_hostZeroRuns;
                m_hostZeroLastLen = m_hostZeroRun;
                m_hostZeroLastSeq = m_txSeq;
                m_hostZeroLastQueue = m_txQueue.size();
            }
            m_hostZeroRun = 0;
        }
        m_ovPeak = std::max(m_ovPeak, p);
        m_ovSumSq += p;
        m_hwDucPk = std::max(m_hwDucPk, p);
        if (++m_hwDucN == kDucSampleRateHz / 2) {   // 0.5 s (PS HW peak)
            m_hwDucLast.store(m_hwDucPk, std::memory_order_relaxed);
            m_hwDucPk = 0.0;
            m_hwDucN = 0;
        }
        m_ovWinSumSq += p;
        // Build 436: the TXA output is made up for the CFIR (Hl2TxDsp), so
        // the wire is on the scale before it: no correction here any more.
        m_envPeak = std::max(m_envPeak, p);
        if (++m_envN == kDucSampleRateHz / 20) {   // 50 ms
            emit txEnvelopePeak(static_cast<float>(10.0 * std::log10(std::max(m_envPeak, 1e-14))));
            m_envPeak = 0.0;
            m_envN = 0;
        }
        m_ovTrSum += p;
        if (++m_ovTrN == kDucSampleRateHz / 4) {   // 250 ms trace
            if (m_ovTrace.size() < 24)
                m_ovTrace.push_back(m_ovTrSum / m_ovTrN);
            m_ovTrSum = 0.0;
            m_ovTrN = 0;
        }
        if (++m_ovWinCount == kDucSampleRateHz / 10) {   // 100 ms
            m_ovWinMax = std::max(m_ovWinMax, m_ovWinSumSq / m_ovWinCount);
            m_ovWinSumSq = 0.0;
            m_ovWinCount = 0;
        }
    }
    m_ovCount += static_cast<quint64>(n);
    {
        std::lock_guard<std::mutex> lock(m_ducSnapMutex);
        if (m_ducSnap.size() != std::size_t(2 * kDucSnap))
            m_ducSnap.assign(std::size_t(2 * kDucSnap), 0.0);
        for (int k = 0; k < n; ++k) {
            const auto& v = buf[k];
            m_ducSnap[std::size_t(2 * m_ducSnapPos)] = v.real();
            m_ducSnap[std::size_t(2 * m_ducSnapPos + 1)] = v.imag();
            m_ducSnapPos = (m_ducSnapPos + 1) % kDucSnap;
        }
        m_ducSnapFill = std::min(kDucSnap, m_ducSnapFill + n);
    }
    // The TX panorama shows exactly these samples (TxScope applies the
    // same clamp as buildDucIq), whatever produced them.
    if (m_scopeFromFb.load(std::memory_order_relaxed)
        && (!m_psEnabled || steadyMs() - m_scopeFbMs.load(std::memory_order_relaxed) > 200))
        m_scopeFromFb.store(false, std::memory_order_relaxed);   // the feedback stopped: back to the DUC IQ
    m_txScope.setAverageMs(m_txScopeAvgMs.load(std::memory_order_relaxed));   // build 346
    if (!m_scopeFromFb.load(std::memory_order_relaxed)
        && m_txScope.push(buf, n, m_txScopeBins)) {
        emitTxScopeFrame(static_cast<double>(m_txHz));
    }
}

// Build 432 (operator, 15.11: two tones, the line at -0.9 dB "though we
// measure before the correction"). OUR ERROR in 425: the TX drive peak line
// is drawn from the TX scope's frame (its last value, TxScope's envelope
// peak of the wire samples: SpectrumWidget::setTxScopeFrame), not from
// txEnvelopePeak, where 425 put the CFIR correction. Here it is: the peak
// divided by the CFIR's passband power (-0.78 dB), TUNE's tone and the host
// CW (made after the CFIR) as they are. The bins stay the wire's.
// Build 436: no correction any more -- the TXA output is made up for the
// CFIR in Hl2TxDsp, so the wire itself is on the scale before it.
void P2Transport::emitTxScopeFrame(double hz)
{
    emit txScopeFrameReady(QList<float>(m_txScopeBins.begin(), m_txScopeBins.end()), hz);
}

void P2Transport::txPumpLocked()
{
    // Protocol 1: the EP2 sender takes the samples on the radio's clock.
    if (!m_mox || m_p1 || !m_sessionUp.load(std::memory_order_acquire))
        return;
    const quint64 lead = quint64(kTxLeadPackets + m_leadAdj.load(std::memory_order_relaxed));
    const quint64 due = m_txDevSamples / kDucSamplesPerPacket + lead;
    // Held up for longer than the FIFO lead covers (the radio has played
    // zeros meanwhile): do not burst the missed packets into its FIFO, just
    // re-establish the lead from now.
    if (due > m_txPacketsSent + lead + 8)
        m_txPacketsSent = due - lead;
    std::complex<float> buf[kDucSamplesPerPacket];
    int burst = 0;
    while (m_txPacketsSent < due && burst < 32) {
        fillTxPacket(buf);
        accountTxSamples(buf, kDucSamplesPerPacket);
        limitTxDrive(buf, kDucSamplesPerPacket);   // build 348
        const auto pkt = buildDucIq(m_txSeq++, std::span<const std::complex<float>>(buf, kDucSamplesPerPacket));
        if (sendNative(pkt.data(), pkt.size(), kDucIqPort) < 0)
            m_nativeSendFails.fetch_add(1, std::memory_order_relaxed);
        ++m_txPacketsSent;
        m_ducSentTotal.fetch_add(1, std::memory_order_relaxed);
        ++burst;
    }
}

void P2Transport::setTxFrequencyHz(std::uint32_t hz)
{
    if (hz == m_txHz)
        return;
    {
        std::lock_guard<std::mutex> lock(m_txMutex);   // read by the DUC sender
        m_txHz = hz;
    }
    if (m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::setTxDriveLevel(int level)
{
    if (!transmitEnabled())
        level = 0;
    level = std::clamp(level, 0, 255);
    if (level == m_drive)
        return;
    m_drive = level;
    m_driveAtomic.store(level, std::memory_order_relaxed);
    // The feedback amplitude is proportional to the drive (the drive byte
    // scales the DAC output): a change of D dB is met by D dB more feedback
    // attenuation, in whole dB once it reaches 1 dB.
    bool attChanged = false;
    if (m_psAttFollowDrive && m_psEnabled && level > 0) {
        if (m_psDriveRef <= 0.0) {
            m_psDriveRef = level;
        } else {
            const double predDb = 20.0 * std::log10(double(level) / m_psDriveRef);
            if (std::abs(predDb) >= 1.0) {
                const int step = int(std::lround(predDb));
                const int newAtt = std::clamp(m_psAttDb + step, 0, 31);
                // Build 837: the reference moves by what the attenuator really
                // moved -- at 0 or 31 dB the rest is owed, so coming back the
                // attenuator does not undershoot (kept across pauses now, a
                // clamped step would otherwise stay wrong for good).
                m_psDriveRef *= std::pow(10.0, (newAtt - m_psAttDb) / 20.0);
                if (newAtt != m_psAttDb) {
                    m_psAttDb = newAtt;
                    m_psAttNow.store(newAtt, std::memory_order_relaxed);
                    attChanged = true;
                }
            }
        }
    }
    // ANAN fork build 837 (operator, 27.11: TUNE with PureSignal overloaded
    // the feedback ADC). The reference used to be forgotten whenever the
    // drive went to 0 -- between every two overs -- so the next over's drive
    // became the new reference with the attenuator left where the last one
    // had it: TUNE at a higher drive than the SSB before it started with the
    // SSB attenuation. The reference is kept across the pauses now; the
    // attenuator follows the drive from over to over.
    if (m_run && m_configured) {
        if (attChanged)
            sendTransmitSpecific();
        sendHighPriority(true);
    }
}

// ANAN fork build 850: the radio's own keyer takes the PC's key (Thetis).
bool P2Transport::fpgaCwxActive() const noexcept
{
    // Not the Hermes-Lite 2 (its own keying, unchanged); not with the keyer
    // off (any mode but CW, TUNE, outside the amateur bands).
    // Build 854 (operator, 27.11): build 853's Hermes-Lite 2 undone -- it has
    // no keyer of its own (HL2 wiki, Protocol: "There is no internal keyer"),
    // only the CWX bit, and MOX must be zero while it sends CW.
    return !isHl2Session() && m_profile.boardId != kBoardIdHermesLite
           && effectiveCwKeyer().enabled;
}

std::uint8_t P2Transport::cwxBitsP1() const noexcept
{
    // Protocol 1 order (Thetis networkproto1.c): dot << 2 | dash << 1 | cwx.
    const std::uint8_t b = m_cwxBits.load(std::memory_order_relaxed);
    return static_cast<std::uint8_t>((b & 0x01) | ((b & 0x04) ? 0x02 : 0) | ((b & 0x02) ? 0x04 : 0));
}

void P2Transport::setCwxLine(bool down)
{
    const std::uint8_t was = m_cwxBits.load(std::memory_order_relaxed);
    const auto now = static_cast<std::uint8_t>(down ? (was | 0x01) : (was & ~0x01));
    if (now == was)
        return;
    m_cwxBits.store(now, std::memory_order_relaxed);
    // Protocol 2: at once (Thetis SetCWX -> CmdHighPriority). Protocol 1:
    // the next EP2 packet carries it (sendP1Ep2), every 2.6 ms.
    if (!m_p1 && m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::clearCwxLines()
{
    if (m_cwxBits.exchange(0, std::memory_order_relaxed) == 0)
        return;
    if (!m_p1 && m_run && m_configured)
        sendHighPriority(true);
}

void P2Transport::resetHostPaddle()
{
    m_hostPaddleHangTimer->stop();
    if (!m_hostPaddleDown && !m_hostPaddleHold)
        return;
    m_hostPaddleDown = m_hostPaddleHold = false;
    updateRadioPtt();
}

void P2Transport::setCwPaddleBits(bool dot, bool dash)
{
    // A press only with the radio's keyer on and transmit allowed; a release
    // always.
    if ((dot || dash) && (!fpgaCwxActive() || !transmitEnabled()))
        dot = dash = false;
    const std::uint8_t was = m_cwxBits.load(std::memory_order_relaxed);
    const auto now = static_cast<std::uint8_t>((was & 0x01) | (dot ? 0x02 : 0) | (dash ? 0x04 : 0));
    if (now != was) {
        m_cwxBits.store(now, std::memory_order_relaxed);
        // Protocol 2 at once (CmdHighPriority); Protocol 1 with the next EP2.
        if (!m_p1 && m_run && m_configured)
            sendHighPriority(true);
    }
    const bool down = dot || dash;
    if (down == m_hostPaddleDown)
        return;
    m_hostPaddleDown = down;
    if (down) {
        m_hostPaddleHangTimer->stop();
        m_hostPaddleHold = true;
    } else if (m_hostPaddleHold) {
        // The keyer finishes the element under way (a dash and its space, and
        // in mode B the one squeezed after it), then its hang time.
        const int wpm = std::clamp(m_cwKeyer.speedWpm, 1, 63);
        const int dits = m_cwKeyer.mode == 2 ? 6 : 4;
        m_hostPaddleHangTimer->start(std::max(10, m_cwKeyer.hangMs) + dits * 1200 / wpm);
    }
    updateRadioPtt();
}

void P2Transport::setCwKeyDown(bool down, const TxCoordinator::Operation& operation)
{
    // Build 850 (Thetis SetCWX): with the radio's keyer on, the CWX line --
    // no host carrier. A release always goes through, whatever the operation.
    if (!down && m_cwxBits.load(std::memory_order_relaxed) != 0)
        setCwxLine(false);
    if (fpgaCwxActive()) {
        if (!down)
            return;
        if (!TxCoordinator::Command{operation, down}.permitsDispatch(TxCoordinator::monotonicMs()))
            return;
        if (!transmitEnabled())
            return;
        {
            std::lock_guard<std::mutex> lock(m_txMutex);
            m_cwMode = false;   // no host carrier under the radio's own
            m_cwDown = false;
            m_cwEnv = 0.0;
        }
        setCwxLine(true);
        return;
    }
    if (!TxCoordinator::Command{operation, down}.permitsDispatch(TxCoordinator::monotonicMs()))
        return;
    if (down && !transmitEnabled())
        return;
    std::lock_guard<std::mutex> lock(m_txMutex);
    if (!down && !m_cwMode)
        return;
    if (down && !m_cwMode) {
        m_txQueue.clear();
        m_cwEnv = 0.0;
    }
    m_cwMode = true;
    m_cwDown = down;
}

void P2Transport::clearCwKeying()
{
    {
        std::lock_guard<std::mutex> lock(m_txMutex);
        m_cwMode = false;
        m_cwDown = false;
        m_cwEnv = 0.0;
    }
    clearCwxLines();   // build 850
}

void P2Transport::queueTxIq(std::span<const std::complex<float>> iq,
                            const TxCoordinator::Context& context)
{
    if (!context.permitsDispatch(TxCoordinator::monotonicMs()))
        return;
    // Any thread (the transmit thread, AnanTxEngine).
    std::lock_guard<std::mutex> lock(m_txMutex);
    if (!m_mox)
        return;   // unkeyed: a late block of the last over, not the next one's
    if (!m_txContext.sameContext(context)) {
        m_txQueue.clear();
        m_txContext = context;
    }
    for (const auto& s : iq)
        m_txQueue.push_back(s);
    while (m_txQueue.size() > kTxQueueMax)
        m_txQueue.pop_front();
}

void P2Transport::flushTxIq()
{
    std::lock_guard<std::mutex> lock(m_txMutex);
    m_txQueue.clear();
    m_txPrimed = false;
}

void P2Transport::setTxTestTone(double offsetHz, double amplitude,
                                const TxCoordinator::Operation& operation)
{
    if (!TxCoordinator::Command{operation, amplitude > 0.0}.permitsDispatch(TxCoordinator::monotonicMs()))
        return;
    std::lock_guard<std::mutex> lock(m_txMutex);
    m_toneHz = offsetHz;
    m_toneAmp = std::clamp(amplitude, 0.0, 1.0);
    if (m_toneAmp == 0.0)
        m_tonePhase = 0.0;
}

// ---- Protocol 1 (build 194) -------------------------------------------------
//
// The same radio state as Protocol 2, carried the Protocol 1 way (Thetis
// ChannelMaster/networkproto1.c): the host sends EP2 packets to port 1024,
// each with two of the 17 C&C registers in rotation and 126 samples of
// speaker audio (and, later, TX I/Q); the radio sends EP6 packets with the
// DDCs' I/Q interleaved, the mic word and its status in the C&C bytes.

int P2Transport::p1RateCode() const noexcept
{
    // Protocol 1 DDCs stop at 384 ksps (C0 0 C1 bits 0-1).
    return std::clamp(static_cast<int>(m_p.sampleRate), 0, 3);
}

P1CcState P2Transport::p1State() const
{
    P1CcState s;
    s.mox = m_mox;
    s.rateCode = p1RateCode();
    s.nddc = m_p1Plan.nddc;
    const std::uint32_t rx0 = m_rxHz[0];
    const std::uint32_t rx1 = m_numRx > 1 ? m_rxHz[1] : rx0;
    const std::uint32_t tx = m_txHz ? m_txHz : rx0;
    // Thetis WriteMainLoop: the DDCs not carrying a receiver follow RX1
    // (ANGELIA's DDC1, the diversity partner) or the TX frequency (the
    // PureSignal pair).
    s.ddcHz.fill(rx0);
    if (s.nddc == 5) {
        s.ddcHz[1] = rx0;
        s.ddcHz[3] = tx;
        s.ddcHz[4] = tx;
    } else if (s.nddc == 4) {
        s.ddcHz[2] = tx;
        s.ddcHz[3] = tx;
    }
    s.ddcHz[static_cast<std::size_t>(m_p1Plan.rxDdc[0])] = rx0;
    s.ddcHz[static_cast<std::size_t>(m_p1Plan.rxDdc[1])] = rx1;
    // Build 468: RX3 / RX4 on their DDCs.
    for (int i = 2; i < m_numRx && i < m_p1Plan.maxRx; ++i)
        s.ddcHz[static_cast<std::size_t>(m_p1Plan.rxDdc[static_cast<std::size_t>(i)])] =
            m_rxHz[static_cast<std::size_t>(i)];
    // Hermes II (nddc 2): PureSignal borrows the receivers' DDC0/DDC1 while
    // transmitting (Thetis UpdateDDCs), both on the TX frequency.
    const bool psTx = m_psEnabled && m_mox;
    // Build 468: the PureSignal pair back on the TX frequency while it
    // transmits, a receiver's or not.
    if (psTx && m_p1Plan.nddc > 2 && m_p1Plan.psFbDdc >= 0) {
        s.ddcHz[static_cast<std::size_t>(m_p1Plan.psFbDdc)] = tx;
        s.ddcHz[static_cast<std::size_t>(m_p1Plan.psFbDdc + 1)] = tx;
    }
    if (s.nddc == 2 && psTx) {
        s.ddcHz[0] = tx;
        s.ddcHz[1] = tx;
    }
    // Build 504 (operator, 18.11: "it is not the PureSignal bit, it is the
    // relay of the internal tap"). OUR ERROR in 503: the option raised both
    // PureSignal bits. Thetis sends puresignal_run in two places: C0 0x14 C2
    // bit 6, PureSignal itself, and C0 0x24 C2 bit 6 in the ORION MkII
    // filter-board word (Protocol 2: Alex bit 18) -- the coupler's relay.
    // "PS Coupler Always when TX" raises only the relay's bit on transmit.
    s.pureSignal = m_psEnabled;
    // Build 505: back to both bits following PureSignal only, as before 503.
    s.psCouplerRelay = m_psEnabled;
    s.txHz = tx;
    // The ADC for each receiver's DDC (2 bits per DDC); ANGELIA's DDC1 goes
    // with RX1. Single-ADC boards: all zero.
    const int lastAdc = std::max(0, m_profile.numAdcs - 1);
    const auto adcOf = [&](int rx) { return std::min(m_rxAdc[static_cast<std::size_t>(rx)], lastAdc); };
    std::uint16_t adc = 0;
    const auto setAdc = [&adc](int ddc, int a) {
        adc = static_cast<std::uint16_t>((adc & ~(3u << (2 * ddc))) | (unsigned(a & 3) << (2 * ddc)));
    };
    setAdc(m_p1Plan.rxDdc[0], adcOf(0));
    if (s.nddc == 5)
        setAdc(1, adcOf(0));
    if (m_numRx > 1)
        setAdc(m_p1Plan.rxDdc[1], adcOf(1));
    // Build 468: RX3 / RX4; not the PureSignal pair while it transmits (the
    // feedback stays where PureSignal has it).
    for (int i = 2; i < m_numRx && i < m_p1Plan.maxRx; ++i) {
        const int d = m_p1Plan.rxDdc[static_cast<std::size_t>(i)];
        if (psTx && m_p1Plan.psFbDdc >= 0 && (d == m_p1Plan.psFbDdc || d == m_p1Plan.psFbDdc + 1))
            continue;
        setAdc(d, adcOf(i));
    }
    s.adcControl = adc;
    // The filters follow the first receiver on ADC0, as on Protocol 2.
    std::size_t filterRx = 0;
    for (int i = 0; i < m_numRx; ++i) {
        if (effectiveAdc(i) == 0) {
            filterRx = static_cast<std::size_t>(i);
            break;
        }
    }
    const double filterHz = static_cast<double>(m_rxHz[filterRx]);
    s.openCollector = openCollectorBits(m_oc, ocRxHz(filterRx), static_cast<double>(tx), m_mox,
                                        m_tunerStart);
    // Build 439: Protocol 1 has one Dither / Random pair (C0=0 C3 bits 3 / 4,
    // Thetis adc[0]): ADC1's setting.
    s.dither = (m_ditherMask & 1) != 0;
    s.random = (m_randomMask & 1) != 0;
    s.antenna = m_mox ? m_txAnt : m_rxAnt;
    s.duplex = true;
    // With PureSignal on, ADC0's transmit attenuation is the feedback
    // attenuator (Thetis SetTxAttenData from the PS auto-attenuate).
    // Build 561: "Full ATT RX1 when TX" off -- ADC0's receive attenuation.
    s.txAdc0AttDb = m_psEnabled ? m_psAttDb : (m_adc0FullAttOnTx ? m_txAttDb : m_adcAtt[0]);
    // PS feedback input while transmitting, as on Protocol 2 (C0=0 C3: bit 7
    // _Rx_1_Out, bit 6 _Rx_2_In = EXT1, bit 5 _Rx_1_In = EXT2): internal
    // coupler = no RX input bit; EXT1 = EXT1 + RX bypass out; RX bypass = RX
    // bypass out on the Alex boards ("RX Out on Tx"), RX 1 in + RX bypass out
    // on ORION MkII (Thetis "Rx BYPASS on Tx" = Ext2OutOnTx). Build 427: OUR
    // ERROR -- ORION MkII got bit 7 only for both, so its input relay never
    // switched (operator, 15.11, a 7000DLE on Protocol 1).
    // Build 438 (operator, 15.11: the user watches his own signal in duplex):
    // the chosen feedback input is switched on every transmission, PureSignal
    // on or off -- as Thetis' "Ext 1 on Tx" / "Rx BYPASS on Tx", which are
    // independent of PS.
    // Build 502: with PureSignal on, or on every transmission with "PS Coupler
    // Always when TX" (build 504: the coupler relay bit goes with it, above).
    if (m_mox && m_psSource != 0 && (m_psEnabled || m_psCouplerOnTx)) {
        s.rxInputBits = 0x80;
        if (m_psSource == 1)
            s.rxInputBits |= 0x40;
        else if (m_profile.alexScheme == AlexScheme::Orion2BandPass)
            s.rxInputBits |= 0x20;
    }
    s.driveLevel = cwDemoActive() ? 0 : m_drive;   // CW demo: no RF
    s.micBoost = m_micBoost;
    // The receiver on ADC1 (else the ADC0 one): its frequency picks the ADC1
    // band-pass bank (C0 0x24 C1).
    // Build 279: the banks per ADC, bypassed when their receivers span bands.
    const BankChoice bank0 = bankFor(0);
    const BankChoice bank1 = bankFor(1);
    // Build 267 (replaces build 245's "Forced RXx filter control like 7000"):
    // a board with firmware modified for 7000DLE-style band-pass banks ahead
    // of both ADCs is forcedly assigned as a 7000DLE, which gives it the ORION
    // MkII profile and this filter logic.
    const bool orion2Filters = m_profile.alexScheme == AlexScheme::Orion2BandPass;
    if (orion2Filters) {
        // ORION MkII: band-pass banks per ADC, the LPF on the TX frequency.
        const AlexRxFilter f0 = bank0.used ? bank0.filter : alexRxFilterFor(filterHz);
        // Build 396: Thetis' "BPF ByPass on PS", as on Protocol 2.
        // Build 685: the Wideband Monitor's BPF Bypass on Protocol 1 as well
        // (the window works on both protocols; build 683 did Protocol 2 only).
        s.alexHpf = p1AlexHpfBits((psBypassesBank(0) || (m_wbBypassMask & 1))
                                      ? AlexRxFilter::Bypass : f0);
        s.alex2Hpf = p1AlexHpfBits((psBypassesBank(1) || (m_wbBypassMask & 2))
                                       ? AlexRxFilter::Bypass
                                       : (bank1.used ? bank1.filter : f0));
        s.alexLpf = p1AlexLpfBitsFor(static_cast<double>(tx));
        s.rx2GroundOnTx = m_adc1GndOnTx;
    } else {
        // ANAN-10/100/100D/200D: high-pass ahead of ADC0, and the receive
        // path through the TX low-pass bank, which therefore follows the
        // receive frequency while receiving (as on Protocol 2).
        const bool wbBypass0 = (m_wbBypassMask & 1) != 0;   // build 685
        s.alexHpf = p1AlexHpfBits((psBypassesBank(0) || wbBypass0) ? AlexRxFilter::Bypass   // build 502
                                  : bank0.used ? bank0.filter : legacyAlexHpfFor(filterHz));
        // The PA's harmonic filter on the TX frequency while transmitting;
        // receiving, ADC1's receiver's band, or (build 280) the widest when
        // the bank is bypassed -- as Protocol 2 sends it (deskHPSDR).
        s.alexLpf = p1AlexLpfBitsFor(m_mox ? static_cast<double>(tx)
                                   : (bank0.spanned || wbBypass0) ? std::numeric_limits<double>::infinity()
                                   : (bank0.used ? bank0.lpfHz : filterHz));
    }
    if (m_lna && !m_mox)
        s.alexHpf |= 0x40;   // 6 m preamp, the LNA button
    s.paEnabled = paEffective();
    s.xvtrEnable = m_xvtrTx;   // build 637
    // Mic connector (Thetis prn->mic: TRS bit 4, bias bit 5, PTT bit 6).
    s.micControl = static_cast<std::uint8_t>(((m_micControl & kMicTipIsPtt) ? 0x10 : 0)
                                             | ((m_micControl & kMicBiasOn) ? 0x20 : 0)
                                             | ((m_micControl & kMicPttDisabled) ? 0x40 : 0));
    s.rxAttDb = {m_adcAtt[0], m_adcAtt[1], 0};
    // Build 430 (operator, 15.11: a 7000DLE on Protocol 1, firmware 2.1 --
    // the user watches the attenuator's bus on the board: the receive
    // setting reaches the chip, the PS slider does not on transmit). The log
    // of build 429 shows C0 0x1C C3 (the transmit attenuator, as Thetis
    // sends it) following the slider 0..31, so this firmware does not apply
    // it. While transmitting with PureSignal the feedback ADC's own step
    // attenuator byte (C0 0x14 C4 for ADC0, C0 0x16 C1 for ADC1) carries the
    // PS attenuation too: a firmware that uses 0x1C C3 on transmit ignores
    // these then, one that does not gets the PS value from them. The receive
    // values come back with MOX off.
    // Build 431 (operator, 15.11: "there is also the attenuator value during
    // transmission"): the same for it -- with PureSignal off, ADC0's byte
    // carries the transmit attenuator (Radio Setup) while transmitting, the
    // value 0x1C C3 carries; so ADC0 is attenuated on transmit as the
    // operator set it whichever byte the firmware takes.
    if (m_mox)
        s.rxAttDb[0] = s.txAdc0AttDb;
    // Build 542: "Full ATT RX2 when TX" (see setAdc1FullAttOnTx).
    s.txAdc1AttDb = m_adc1FullAttOnTx ? 31 : m_adcAtt[1];
    if (psTx && m_psAdc == 1)
        s.txAdc1AttDb = m_psAttDb;
    s.cwKeyer = effectiveCwKeyer().enabled;   // build 224: off outside the amateur bands
    s.cwPaddleSwap = m_cwKeyer.paddleSwap;
    s.cwMode = m_cwKeyer.mode;
    s.cwSpeedWpm = m_cwKeyer.speedWpm;
    s.cwWeight = m_cwKeyer.weight;
    s.cwStrictSpacing = m_cwKeyer.strictSpacing;
    s.cwSidetoneLevel = m_cwKeyer.sidetoneLevel / 2;   // 0..255 -> 0..127
    s.cwRfDelayMs = m_cwKeyer.rfDelayMs;
    s.cwHangMs = m_cwKeyer.hangMs;
    s.cwSidetoneHz = m_cwKeyer.sidetoneHz;
    // ANAN fork build 723: Hermes-Lite 2 -- its own rotation (p1CcHl2). The
    // LNA gain is the interface's "LNA gain" itself (setLnaGainDb), receive
    // and transmit alike for now.
    if (m_profile.boardId == kBoardIdHermesLite) {
        s.hermesLite = true;
        // The open-collector byte is the filter board's (N2ADR), which the
        // backend chooses per frequency (Hl2Backend::applyBandFilter ->
        // setBandFilter), not the ANAN User Out matrix.
        s.openCollector = static_cast<std::uint8_t>(m_p.ocFilterByte & 0x7F);
        // Build 724: the LNA is the attenuator engine's ADC0 value, 48 - att
        // (Hl2Backend::ananPushAttenuators -> setAdcAttenuation).
        s.hl2RxGainDb = std::clamp(kHl2GainAtZeroAttDb - m_adcAtt[0], -12, 48);
        // Build 725: the drive byte's hardware step; the rest scales the I/Q
        // (sendP1Ep2).
        s.driveLevel = hl2DriveSplit(s.driveLevel).hwLevel;
        // Build 726: the settings (Quisk): the transmit LNA, the PA, the
        // timings, the config bits.
        s.hl2TxGainDb = m_hl2.txLnaDb;
        s.paEnabled = s.paEnabled && m_hl2.powerAmp;
        s.hl2LowPwrTrDisable = m_hl2.lowPwrTrDisable;
        s.hl2AtuTune = m_hl2AtuTune;
        s.hl2PttHangMs = m_hl2.pttHangMs;
        s.hl2TxLatencyMs = m_hl2.txLatencyMs;
        s.hl2ResetOnDisconnect = m_hl2.resetOnDisconnect;
        s.hl2Bit11 = m_dither;   // Hl2Backend: AK4951 -> speaker on, else PWM band indicator
        s.hl2DisableSync = m_hl2.disableSync;
        // ANAN fork build 744 (operator, 25.11: PureSignal on the HL2, "very
        // carefully"). deskHPSDR old_protocol.c (DEVICE_HERMES_LITE2): with
        // PureSignal the HL2 has no feedback attenuator of its own -- the
        // feedback is its receiver's ADC on DDC2 (the TX DAC on DDC3), so the
        // PS attenuation 0..31 dB is the LNA gain code 31 - att while
        // transmitting (C0 0x1C C3 = 0xC0 | code and C0 0x14 C4 = 0x40 |
        // code; MI0BOT's Thetis sends its tx_step_attn in 0x14 C4 on transmit
        // as well): gain = 19 - att dB. Receiving, nothing changes.
        if (psTx) {
            const int psGainDb = std::clamp(19 - m_psAttDb, -12, 48);
            s.hl2TxGainDb = psGainDb;
            s.hl2RxGainDb = psGainDb;
        }
        // deskHPSDR: TX buffer latency 40 ms whenever PureSignal is on
        // ("Safety first: TUNE / PureSignal -> conservative"); never less than
        // the operator's setting.
        if (m_psEnabled)
            s.hl2TxLatencyMs = std::max(s.hl2TxLatencyMs, 40);
    }
    // Build 751: the attenuation these C&C words give each ADC (the HL2: its
    // LNA gain as attenuation below +48 dB, as the attenuator control shows).
    if (s.hermesLite)
        publishAttNow(kHl2GainAtZeroAttDb - (m_mox ? s.hl2TxGainDb : s.hl2RxGainDb),
                      kHl2GainAtZeroAttDb - (m_mox ? s.hl2TxGainDb : s.hl2RxGainDb));
    else if (m_mox)
        publishAttNow(s.txAdc0AttDb, s.txAdc1AttDb);
    else
        publishAttNow(std::clamp(m_adcAtt[0], 0, kMaxStepAttenuationDb),
                      std::clamp(m_adcAtt[1], 0, kMaxStepAttenuationDb));
    return s;
}

void P2Transport::sendP1Start()
{
    if (!m_sock)
        return;
    // Thetis ForceCandCFrame + SendStartToMetis: the configuration and the
    // frequencies first, so the radio starts on them, then "start I/Q".
    const P1CcState st = p1State();
    for (int i = 0; i < 2; ++i) {
        const hl2::Cc a = p1Cc(0, st);
        const hl2::Cc b = p1Cc(i == 0 ? 1 : 2, st);
        sendDatagram(*m_sock, hl2::ep2Packet(m_p1Seq++, a, b), m_txHost, kRadioPort,
                     m_counters.txBytes, m_counters.txPackets);
    }
    if (isHl2Session())
        queueHl2SessionSetup();   // build 726
    // Bit 0: I/Q + mic; bit 1: the wide bandscope (Metis "How it works").
    sendDatagram(*m_sock, hl2::metisCommand((m_wb.enableMask & 0x01) ? 0x03 : 0x01), m_txHost,
                 kRadioPort, m_counters.txBytes, m_counters.txPackets);
}

void P2Transport::sendP1Ep2()
{
    if (!m_sock)
        return;
    const P1CcState st = p1State();
    // Build 723: the rotation's length per board (19 on a Hermes-Lite 2).
    const int ccCount = p1CcCount(st);
    m_p1CcIdx %= ccCount;
    const hl2::Cc a = p1Cc(m_p1CcIdx, st);
    m_p1CcIdx = (m_p1CcIdx + 1) % ccCount;
    hl2::Cc b;
    // Build 727: the N2ADR IO board's receive frequency code (register 13,
    // RX1), as Quisk (IOBoard.NewRxFreq) and deskHPSDR send it, when the
    // code changes.
    if (st.hermesLite) {
        const std::uint8_t code = hl2IoBoardFreqCode(static_cast<double>(m_rxHz[0]));
        if (code != m_hl2IobRxCode) {
            m_hl2IobRxCode = code;
            m_hl2Once.push_back(hl2::ccI2c2Write(hl2::kIoBoardI2cAddr, 13, code));
        }
    }
    // Build 726: a Hermes-Lite 2's one-shot (I2C, watchdog) in place of the
    // second bank, every other packet (the gateware's I2C needs the time;
    // Thetis MI0BOT waits 5 frames, deskHPSDR sends one per HL2 slot).
    m_hl2OnceGap = !m_hl2OnceGap;
    if (st.hermesLite && !m_hl2Once.empty() && m_hl2OnceGap) {
        b = m_hl2Once.front();
        m_hl2Once.pop_front();
        b[0] = static_cast<std::uint8_t>((b[0] & 0xFE) | (st.mox ? 0x01 : 0x00));
    } else {
        b = p1Cc(m_p1CcIdx, st);
        m_p1CcIdx = (m_p1CcIdx + 1) % ccCount;
    }
    // Build 426 (operator, 15.11: a 7000DLE on Protocol 1, the PS feedback on
    // ADC1, "the bypass relay does not switch"): what the radio is told on
    // transmit, once per transmission -- the board profile and the C&C bytes
    // that select the receive filters and the PureSignal inputs.
    // Build 429 (operator, 15.11: the feedback comes now, but "no ATT RX1
    // control from the PS window"): the attenuators too -- 0x1C C3 the ADC0
    // transmit attenuator (the PS one), 0x14 C4 ADC0's receive one, 0x16 C1
    // ADC1's -- and the line again whenever the transmit attenuator changes.
    const bool attChanged = st.mox && st.txAdc0AttDb != m_p1CcLoggedAtt;
    if (!st.mox)
        m_p1CcLoggedAtt = -1;
    if (st.mox && !st.hermesLite   // build 723: the ANAN registers' meanings
        && (m_p1CcLogPending.exchange(false, std::memory_order_relaxed) || attChanged)) {
        m_p1CcLoggedAtt = st.txAdc0AttDb;
        const hl2::Cc at = p1Cc(4, st);   // C0 0x1C: C3 ADC0 transmit attenuator
        const hl2::Cc a1 = p1Cc(12, st);  // C0 0x16: C1 ADC1 attenuator
        const hl2::Cc g = p1Cc(0, st);    // C0 0x00: C3 bit 7 RX bypass out, bits 5-6 RX inputs
        const hl2::Cc f = p1Cc(10, st);   // C0 0x12: C3 BPF1 / HPF (bit 5 bypass), C4 LPF
        const hl2::Cc p = p1Cc(11, st);   // C0 0x14: C2 bit 6 PureSignal
        const hl2::Cc f2 = p1Cc(16, st);  // C0 0x24: C1 BPF2 (bit 5 bypass), C2 bit 6 PureSignal
        const auto hx = [](std::uint8_t v) {
            return QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0'));
        };
        qCInfo(lcAnanP2T).nospace()
            << "ANAN P1 transport: TX C&C on " << m_profile.modelName << " (board "
            << int(m_profile.boardId) << ", band-pass banks "
            << (m_profile.alexScheme == AlexScheme::Orion2BandPass ? "yes" : "no")
            << "), PureSignal " << (m_psEnabled ? "on" : "off") << ", feedback ADC"
            << m_psAdc + 1 << ", source " << m_psSource << ": 0x00 C3=" << hx(g[3])
            << ", 0x12 C3=" << hx(f[3]) << " C4=" << hx(f[4]) << ", 0x14 C2=" << hx(p[2])
            << ", 0x24 C1=" << hx(f2[1]) << " C2=" << hx(f2[2]) << "; PS att " << m_psAttDb
            << " dB: 0x1C C3=" << hx(at[3]) << ", 0x14 C4=" << hx(p[4]) << ", 0x16 C1=" << hx(a1[1]);
    }
    auto pkt = hl2::ep2Packet(m_p1Seq++, a, b);
    // Speaker audio, 126 stereo frames, if the mixer has produced them.
    std::array<std::int16_t, 2 * hl2::kTxSamplesPerPacket> audio{};
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(m_spkMutex);
        if (m_spkQueue.size() >= audio.size()) {
            // Thetis sendProtocol1Samples swaps L and R ("firmware bug fix").
            for (std::size_t i = 0; i < audio.size(); i += 2) {
                audio[i + 1] = m_spkQueue.front();
                m_spkQueue.pop_front();
                audio[i] = m_spkQueue.front();
                m_spkQueue.pop_front();
            }
            have = true;
        }
    }
    // Build 726: not into a Hermes-Lite 2 without a codec board (the slot is
    // not audio there).
    if (have && (!st.hermesLite || m_hl2Codec)) {
        hl2::ep2WriteTxAudio(pkt, std::span<const std::int16_t>(audio.data(), audio.size()));
        m_diagSpkPkts.fetch_add(1, std::memory_order_relaxed);
    }
    // Transmit I/Q: 504 samples of the 192 ksps TX path (tone, CW, RF Delay,
    // rise and priming as on Protocol 2), decimated to the 126 of this packet.
    {
        std::lock_guard<std::mutex> lock(m_txMutex);
        if (st.mox) {
            constexpr int kIn = hl2::kTxSamplesPerPacket * P1TxDecimator::kFactor;   // 504
            std::array<std::complex<float>, kIn> buf;
            std::array<std::complex<float>, hl2::kTxSamplesPerPacket> iq48;
            fillTxPacket(buf.data(), kIn);
            accountTxSamples(buf.data(), kIn);
            m_p1TxDec.process(buf.data(), kIn, iq48.data());
            limitTxDrive(iq48.data(), static_cast<int>(iq48.size()));   // build 348
            // Build 725: a Hermes-Lite 2's fine drive (hl2DriveSplit).
            if (st.hermesLite) {
                const float k = static_cast<float>(
                    hl2DriveSplit(cwDemoActive() ? 0 : m_drive).iqScale);
                for (auto& v : iq48)
                    v *= k;
            }
            hl2::ep2WriteTxIq(pkt, std::span<const std::complex<float>>(iq48.data(), iq48.size()));
            ++m_txPacketsSent;
            m_ducSentTotal.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // Build 850 (Thetis networkproto1.c, cw_enable): with the radio's keyer
    // on, the I/Q words carry the PC's key lines -- on receive too, so a
    // CWX key reaches the keyer before MOX. Not a Hermes-Lite 2.
    if (st.cwKeyer && !st.hermesLite)
        hl2::ep2WriteTxCwBits(pkt, cwxBitsP1());
    sendDatagram(*m_sock, pkt, m_txHost, kRadioPort, m_counters.txBytes, m_counters.txPackets);
}

// PureSignal: one block of synchronised pairs in m_psTxBuf / m_psRxBuf
// (interleaved I/Q, 192 ksps) to the PS engine -- Protocol 2's DDC0/DDC1
// packets, and since build 213 Protocol 1's PS DDC pair resampled to 192 ksps.
void P2Transport::deliverPsPairs()
{
    ++m_psPackets;
    // ANAN-100D port (01.11): used as received. The PS DDC pairs
    // already have the host TXA IQ's handedness -- on a working
    // radio the two-tone lies on the same side in both, and
    // PureSignal reaches -45 dBc; conjugating them (anan114) gave
    // -32 (AM/PM learnt with the wrong sign).
    double hwPeak = -1.0;
    const int pairs = int(m_psRxBuf.size() / 2);
    for (int i = 0; i < pairs; ++i) {
        const double p = m_psTxBuf[std::size_t(2 * i)] * m_psTxBuf[std::size_t(2 * i)]
                         + m_psTxBuf[std::size_t(2 * i + 1)] * m_psTxBuf[std::size_t(2 * i + 1)];
        m_hwFbPk = std::max(m_hwFbPk, p);
        const double pr = m_psRxBuf[std::size_t(2 * i)] * m_psRxBuf[std::size_t(2 * i)]
                          + m_psRxBuf[std::size_t(2 * i + 1)] * m_psRxBuf[std::size_t(2 * i + 1)];
        m_scopeFbPk = std::max(m_scopeFbPk, pr);
        if (++m_hwFbN == kPsFeedbackRateKsps * 500) {   // 0.5 s
            // Only on a strong signal (>= -6 dBFS sent), so the
            // ratio is not noise over noise.
            const double ducLast = m_hwDucLast.load(std::memory_order_relaxed);
            if (ducLast >= 0.25) {
                hwPeak = std::sqrt(m_hwFbPk / ducLast);
                if (m_scopeFbPk > 1e-12)
                    m_scopeFbGain = std::sqrt(ducLast / m_scopeFbPk);
            }
            m_hwFbPk = 0.0;
            m_scopeFbPk = 0.0;
            m_hwFbN = 0;
        }
    }
    m_psSink(m_psTxBuf.data(), m_psRxBuf.data(), pairs, hwPeak);
    // ANAN-100D port (operator, 01.11: two tones "with intermods"
    // on the TX panorama): the DUC IQ carries the PureSignal
    // predistortion -- by design it holds the products that cancel
    // the amplifier's. While PureSignal feeds, the TX panorama
    // shows the coupler instead (DDC0, the RF actually sent), at
    // the DUC level (the peaks matched every 0.5 s).
    if (m_scopeFbGain > 0.0) {
        m_scopeFbBuf.resize(std::size_t(pairs));
        const float g = static_cast<float>(m_scopeFbGain);
        for (int i = 0; i < pairs; ++i)
            m_scopeFbBuf[std::size_t(i)] = {g * float(m_psRxBuf[std::size_t(2 * i)]),
                                            g * float(m_psRxBuf[std::size_t(2 * i + 1)])};
        m_scopeFromFb.store(true, std::memory_order_relaxed);
        m_scopeFbMs.store(steadyMs(), std::memory_order_relaxed);
        std::lock_guard<std::mutex> txLock(m_txMutex);   // m_txScope is TX state
        if (m_txScope.push(m_scopeFbBuf.data(), pairs, m_txScopeBins))
            emitTxScopeFrame(static_cast<double>(m_txHz ? m_txHz : m_rxHz[0]));
    }
}

// Build 469 (a user's homebrew transceiver, board id 1 = HERMES, gateware 29,
// one receiver): the log showed "DDC0 50669 samples/s (configured 96000)". The
// plan for HERMES asks for 4 DDCs; the radio sent 2 -- 36 samples per frame,
// 72 per packet, 1333 packets/s -- and they were read as 4 (19 per frame):
// 1333 x 38 = 50666 samples/s of mixed-up I/Q. EP6 carries no DDC count, but
// the packet rate gives it: the radio sends rate / (2 x samples per frame)
// packets a second, samples per frame = 504 / (6 x nddc + 2). Counted from
// 0.3 s to 1.3 s after the first packet; a count that fits another nddc
// within 3 % replaces the plan's for the session (the radio is told the same
// in C4 from then on). A board that streams what the plan asks is unchanged.
void P2Transport::p1CheckDdcCount()
{
    if (m_p1RateChecked)
        return;
    const qint64 now = TxCoordinator::monotonicMs();
    if (!m_p1RateCheckFromMs) {
        m_p1RateCheckFromMs = now + 300;   // past the start-up burst
        return;
    }
    if (now < m_p1RateCheckFromMs)
        return;
    ++m_p1RateCheckPkts;
    const qint64 span = now - m_p1RateCheckFromMs;
    if (span < 1000)
        return;
    m_p1RateChecked = true;
    const double pps = 1000.0 * double(m_p1RateCheckPkts) / double(span);
    const int rateHz = hl2::sampleRateHz(static_cast<hl2::SampleRate>(p1RateCode()));
    int best = 0;
    double bestErr = 1e9;
    for (int n = 1; n <= 8; ++n) {
        const int spf = static_cast<int>((hl2::kFrameSize - 8) / static_cast<std::size_t>(6 * n + 2));
        const double expect = double(rateHz) / (2.0 * spf);
        const double err = std::abs(pps / expect - 1.0);
        if (err < bestErr) {
            bestErr = err;
            best = n;
        }
    }
    if (best == m_p1Plan.nddc || bestErr > 0.03) {
        qCInfo(lcAnanP2T).nospace() << "ANAN P1 transport: " << qRound(pps) << " packets/s at "
                                    << rateHz / 1000 << " ksps -- " << m_p1Plan.nddc
                                    << " DDC as planned" << (best == m_p1Plan.nddc ? "" : " (no clear fit)");
        return;
    }
    const int n = best;
    qCWarning(lcAnanP2T).nospace()
        << "ANAN P1 transport: the radio streams " << n << " DDC, not the " << m_p1Plan.nddc
        << " of the board's plan (" << qRound(pps) << " packets/s at " << rateHz / 1000
        << " ksps) -- reading " << n << " DDC from now on";
    m_p1Plan.nddc = n;
    for (int i = 0; i < 4; ++i)
        m_p1Plan.rxDdc[static_cast<std::size_t>(i)] = i < n ? i : 0;
    m_p1Plan.maxRx = std::min(n, 4);
    m_p1Plan.psFbDdc = n == 5 ? 3 : n == 4 ? 2 : n == 2 ? 0 : -1;
    m_p1Ddc.assign(static_cast<std::size_t>(n), {});
    m_p1MicPhase = 0;
    m_p1Mic.clear();
    for (auto& q : m_pending)
        q.clear();
    updateHeldMask();
}

void P2Transport::handleP1Ep6(std::span<const std::uint8_t> bytes)
{
    ++m_p1DiagSeen;
    if (bytes.size() != hl2::kUsbPacketSize || bytes[3] != 0x06) {
        ++m_p1DiagBadSize;
        return;   // EP4 (wideband) and anything else: not yet
    }
    const std::uint32_t seq = (std::uint32_t(bytes[4]) << 24) | (std::uint32_t(bytes[5]) << 16)
                              | (std::uint32_t(bytes[6]) << 8) | std::uint32_t(bytes[7]);
    if (m_expectedSeq[0] && seq != *m_expectedSeq[0]) {
        const std::uint32_t lost = seq - *m_expectedSeq[0];
        ++m_dropTotal;
        ++m_diagGaps;
        if (lost < 100000)
            m_diagLost += lost;
        emit dropsUpdated(m_dropTotal);
        emit rxSequenceGap(lost);
    }
    m_expectedSeq[0] = seq + 1;

    p1CheckDdcCount();   // build 469
    for (auto& v : m_p1Ddc)
        v.clear();
    const int rounds = hl2::ep6SamplesMulti(
        bytes, std::span<std::vector<std::complex<float>>>(m_p1Ddc.data(), m_p1Ddc.size()));
    if (rounds <= 0) {
        ++m_p1DiagNoSync;
        return;
    }
    if (bytes[8 + hl2::kFrameSize] != 0x7f || bytes[8] != 0x7f)
        ++m_p1DiagOneFrame;

    // Status in the C&C bytes of both frames (Thetis MetisReadThreadMainLoop).
    for (const std::size_t fs : {std::size_t{8}, std::size_t{8} + hl2::kFrameSize}) {
        const auto st = parseP1Status(bytes.data() + fs);
        if (!st)
            continue;
        if (st->overloadBits) {
            m_ovlLatch.fetch_or(st->overloadBits & 3u, std::memory_order_acq_rel);
            m_ovlDisplay.fetch_or(st->overloadBits & 3u, std::memory_order_acq_rel);
        }
        if (st->exciterRaw) m_excRaw = *st->exciterRaw;
        if (st->fwdRaw) {
            m_fwdRaw = *st->fwdRaw;
            m_fwdPeakWin = std::max(m_fwdPeakWin, m_fwdRaw);   // build 231
        }
        if (st->revRaw) {
            m_revRaw = *st->revRaw;
            m_revPeakWin = std::max(m_revPeakWin, m_revRaw);
        }
        if (st->userAdc0Raw) m_userAdc0Raw = *st->userAdc0Raw;
        if (st->userAdc1Raw) m_userAdc1Raw = *st->userAdc1Raw;
        if (st->supplyRaw) m_supplyRaw = *st->supplyRaw;
        if (st->firmwareVersion && !m_p1Firmware) {
            m_p1Firmware = st->firmwareVersion;
            qCInfo(lcAnanP2T) << "ANAN P1 transport: radio firmware" << *st->firmwareVersion;
        }
    }
    // Radio PTT and the keyer's paddles, from the last good frame (every
    // frame carries them; P2's "status stopped" release does not apply).
    for (const std::size_t fs : {std::size_t{8} + hl2::kFrameSize, std::size_t{8}}) {
        const auto st = parseP1Status(bytes.data() + fs);
        if (!st)
            continue;
        m_sinceStatus.start();
        m_pttLine = st->ptt;
        const bool paddle = effectiveCwKeyer().enabled && (st->dot || st->dash);
        if (paddle != m_paddleDown) {
            m_paddleDown = paddle;
            if (paddle) {
                m_paddleHangTimer->stop();
                m_paddleHold = true;
            } else if (m_paddleHold) {
                m_paddleHangTimer->start(std::max(10, m_cwKeyer.hangMs));
            }
        }
        updateRadioPtt();
        break;
    }
    // The radio's mic (48 kHz, one word per rate/48k rounds), in 64-sample
    // packets as on Protocol 2: it drives the modulator on the radio's clock
    // (AnanTxEngine -> txDeviceTick).
    {
        const int rateHz = hl2::sampleRateHz(static_cast<hl2::SampleRate>(p1RateCode()));
        p1MicSamples(bytes, m_p1Plan.nddc, std::max(1, rateHz / kCodecRateHz), m_p1MicPhase, m_p1Mic);
        std::size_t used = 0;
        while (m_p1Mic.size() - used >= static_cast<std::size_t>(kCodecFramesPerPacket)) {
            std::vector<float> pkt(m_p1Mic.begin() + static_cast<std::ptrdiff_t>(used),
                                   m_p1Mic.begin() + static_cast<std::ptrdiff_t>(used + kCodecFramesPerPacket));
            used += kCodecFramesPerPacket;
            m_sinceMic.start();
            ++m_diagMicPkts;
            m_micArrived.fetch_add(1, std::memory_order_relaxed);
            emit codecMicReady(pkt);
        }
        m_p1Mic.erase(m_p1Mic.begin(), m_p1Mic.begin() + static_cast<std::ptrdiff_t>(used));
    }

    // The receivers' DDCs into the same alignment queues as Protocol 2.
    // Build 468: not a receiver whose DDC PureSignal holds (silence instead,
    // deliverAlignedBlocks()).
    updateHeldMask();
    const std::uint32_t heldNow = m_heldMask.load(std::memory_order_relaxed);
    for (int i = 0; i < m_numRx; ++i) {
        if (heldNow & (1u << i))
            continue;
        const auto ddc = static_cast<std::size_t>(m_p1Plan.rxDdc[static_cast<std::size_t>(i)]);
        if (ddc >= m_p1Ddc.size())
            continue;
        auto& q = m_pending[static_cast<std::size_t>(i)];
        q.push_back(m_p1Ddc[ddc]);
        if (q.size() > kMaxAheadPackets)
            q.pop_front();
    }
    // PureSignal (build 213, Thetis twist(): the first stream the feedback,
    // the second the TX DAC): nddc 5 DDC3 / DDC4, nddc 4 DDC2 / DDC3,
    // nddc 2 DDC0 / DDC1. Resampled to calcc's 192 ksps.
    const bool psTx = m_psEnabled && m_mox && m_psSink
                      && TxCoordinator::monotonicMs()
                             >= m_psAcceptFromMs.load(std::memory_order_relaxed);   // build 468
    if (psTx) {
        const int fbDdc = m_p1Plan.nddc == 5 ? 3 : m_p1Plan.nddc == 4 ? 2 : 0;
        const auto fb = static_cast<std::size_t>(fbDdc);
        if (fb + 1 < m_p1Ddc.size()) {
            const int rateHz = hl2::sampleRateHz(static_cast<hl2::SampleRate>(p1RateCode()));
            if (!m_p1PsActive || m_p1PsRx.inRateHz() != rateHz) {
                m_p1PsRx.configure(rateHz);
                m_p1PsTx.configure(rateHz);
                if (!m_p1PsActive)
                    qCInfo(lcAnanP2T).nospace() << "ANAN P1 transport: PureSignal feedback DDC" << fbDdc
                                                << ", TX reference DDC" << fbDdc + 1 << ", " << rateHz / 1000
                                                << " -> 192 ksps";
            }
            m_p1PsActive = true;
            const auto& rxv = m_p1Ddc[fb];
            const auto& txv = m_p1Ddc[fb + 1];
            const int n = static_cast<int>(std::min(rxv.size(), txv.size()));
            m_psTxBuf.clear();
            m_psRxBuf.clear();
            m_p1PsRx.process(rxv.data(), n, m_psRxBuf);
            m_p1PsTx.process(txv.data(), n, m_psTxBuf);
            if (!m_psRxBuf.empty() && m_psRxBuf.size() == m_psTxBuf.size())
                deliverPsPairs();
        }
    } else {
        m_p1PsActive = false;
    }
    m_diagSamples += m_p1Ddc[0].size();
    diagTick();
    m_sinceIq.restart();
    if (!m_up) {
        m_up = true;
        m_connectTimer->stop();
        noteBindWorked();
        qCInfo(lcAnanP2T) << "ANAN P1 transport: I/Q streaming," << rounds << "samples per packet per DDC";
        emit linkUp();
    }
    deliverAlignedBlocks();

    // EP2 on the radio's clock: one packet per 126 codec (48 kHz) samples of
    // the time this EP6 packet stands for (Thetis paces EP2 from the audio
    // the radio's own I/Q produces, which comes to the same).
    const int rateHz = hl2::sampleRateHz(static_cast<hl2::SampleRate>(p1RateCode()));
    if (int(m_p.sampleRate) > 3 && !m_p1RateLogged) {
        m_p1RateLogged = true;
        qCWarning(lcAnanP2T) << "ANAN P1 transport: Protocol 1 runs at most 384 ksps; the radio streams 384";
    }
    m_p1AudioAcc += static_cast<quint64>(rounds) * 48000u;
    const quint64 per = static_cast<quint64>(hl2::kTxSamplesPerPacket) * static_cast<quint64>(rateHz);
    while (m_p1AudioAcc >= per) {
        m_p1AudioAcc -= per;
        sendP1Ep2();
    }
}

void P2Transport::handleP1Ep4(std::span<const std::uint8_t> bytes)
{
    if (!(m_wb.enableMask & 0x01) || bytes.size() != hl2::kUsbPacketSize)
        return;
    const std::uint32_t seq = (std::uint32_t(bytes[4]) << 24) | (std::uint32_t(bytes[5]) << 16)
                              | (std::uint32_t(bytes[6]) << 8) | std::uint32_t(bytes[7]);
    const std::uint8_t* p = bytes.data() + 8;
    if (m_p1WbOrder == 0) {
        m_p1WbOrder = 1;
        qCInfo(lcAnanP2T) << "ANAN P1 transport: wide bandscope packets arriving (seq" << seq << ")";
    }
    // 16-bit signed, LITTLE-endian, as cuSDR reads them (cusdr_dataEngine.cpp
    // WideBandDataProcessor: s = (qint8)buf[i+1] << 8 | (quint8)buf[i]); the
    // Metis document gives no byte order.
    for (int i = 0; i < kWidebandSamplesPerPacket; ++i) {
        const auto v = static_cast<std::int16_t>(std::uint16_t(p[2 * i]) | (std::uint16_t(p[2 * i + 1]) << 8));
        m_wbScratch[static_cast<std::size_t>(i)] = static_cast<float>(v) / 32768.0f;
    }
    // "Start reading when the least significant 5 bits of the sequence number
    // are zero. Then accumulate 16,384 words" -- 32 packets (cuSDR the same:
    // (31 & seq) == 0, BIGWIDEBANDSIZE 16384 for Hermes firmware > 11 /
    // Mercury > 32; older firmware sends 4096, not supported). A gap drops it.
    constexpr int kPackets = 32;
    auto& buf = m_wbBuf[0];
    auto& next = m_wbNext[0];
    if ((seq & 0x1Fu) == 0) {
        buf.clear();
        buf.reserve(kPackets * kWidebandSamplesPerPacket);
        next = static_cast<qint64>(seq);
    }
    if (next < 0 || static_cast<qint64>(seq) != next) {
        next = -1;
        return;
    }
    for (float v : m_wbScratch)
        buf.push_back(v);
    ++next;
    if (buf.size() >= kPackets * kWidebandSamplesPerPacket) {
        // The radio sends buffers back to back; the host keeps the frame
        // rate the Wideband Monitor asked for.
        if (!m_p1WbClock.isValid() || m_p1WbClock.elapsed() >= m_wb.updateMs) {
            m_p1WbClock.start();
            ++m_diagWbFrames[0];
            emit widebandFrame(0, buf);
        }
        buf.clear();
        next = -1;
    }
}

}  // namespace AetherSDR::anan
