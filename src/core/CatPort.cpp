#include "CatPort.h"
#include "LogManager.h"
#include "RigctlProtocol.h"
#include "SmartCatProtocol.h"
#include "SmartCatSession.h"
#include "models/RadioModel.h"

#include <utility>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSocketNotifier>
#include <QTimer>
#ifdef HAVE_SERIALPORT
#include <QSerialPort>
#endif

#ifndef Q_OS_WIN
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#ifdef Q_OS_MACOS
#include <util.h>
#else
#include <pty.h>
#endif
#endif

namespace AetherSDR {

// Per-user symlink path — mirrors RigctlPty::defaultSymlinkPath() exactly
// (GHSA-qxhr-cwrc-pvrm: no /tmp cross-user collisions, no TOCTOU window).
QString CatPort::defaultSymlinkPath(int portIndex)
{
    QString leaf;
    if (portIndex >= 0 && portIndex < 8) {
        const char letter = static_cast<char>('A' + portIndex);
        leaf = QStringLiteral("cat-%1").arg(QChar(letter));
    } else {
        leaf = QStringLiteral("cat-%1").arg(portIndex);
    }

    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache/aethersdr");

    if (!base.contains(QStringLiteral("aethersdr"), Qt::CaseInsensitive))
        base += QStringLiteral("/aethersdr");

    return base + QChar('/') + leaf;
}

CatPort::CatPort(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{}

CatPort::~CatPort()
{
    stop();
}

bool CatPort::start(quint16 port)
{
    if (isRunning()) return true;
    if (port == 0 && m_serialName.isEmpty()) return false;

    if (port != 0) {
        m_tcpServer = new QTcpServer(this);
        if (!m_tcpServer->listen(QHostAddress::Any, port)) {
            qCWarning(lcCat) << "CatPort: failed to bind port" << port
                             << m_tcpServer->errorString();
            delete m_tcpServer;
            m_tcpServer = nullptr;
            // The serial endpoint does not depend on the TCP port.
            if (m_serialName.isEmpty())
                return false;
        } else {
            connect(m_tcpServer, &QTcpServer::newConnection,
                    this, &CatPort::onNewConnection);
            qCInfo(lcCat) << "CatPort: listening on port" << port
                          << "dialect" << static_cast<int>(m_dialect);
        }
    }

    m_running = true;
    startPty();
    openSerial();
    return true;
}

void CatPort::stop()
{
    m_running = false;
    closeSerial();
    stopPty();

    // Swap out the client list before iterating: abort() emits disconnected
    // synchronously, and onRigctlDisconnected calls removeAt() on m_rigctlClients,
    // which would invalidate the range-for with 2+ clients. Disconnecting signals
    // first prevents that re-entrant mutation and the double-free it causes.
    // Also fixes the original UAF: Qt parents sockets to m_tcpServer, so
    // delete m_tcpServer frees them — abort() after that is a use-after-free.
    auto clients = std::exchange(m_rigctlClients, {});
    for (auto& c : clients) {
        QObject::disconnect(c.socket, nullptr, this, nullptr);
        c.socket->abort();
        delete c.protocol;
    }

    if (m_tcpServer) {
        m_tcpServer->close();
        delete m_tcpServer;
        m_tcpServer = nullptr;
    }

    // Close all SmartCAT sessions (sessions own their sockets)
    for (auto* s : m_catSessions)
        s->deleteLater();
    m_catSessions.clear();

    emit clientCountChanged(0);
}

bool CatPort::isRunning() const
{
    return m_running;
}

quint16 CatPort::port() const
{
    return m_tcpServer ? m_tcpServer->serverPort() : 0;
}

int CatPort::clientCount() const
{
    return m_rigctlClients.size() + m_catSessions.size();
}

// ── Serial endpoint ─────────────────────────────────────────────────────────
// Windows: a COM port; with a virtual null-modem pair (com0com / VSPE) the
// logger opens the other end. Linux: a tty device (the PTY above is the
// virtual one). 8N1, no flow control; the baud matters only on a real port.

bool CatPort::serialSupported()
{
#ifdef HAVE_SERIALPORT
    return true;
#else
    return false;
#endif
}

bool CatPort::serialOpen() const
{
#ifdef HAVE_SERIALPORT
    return m_serial && m_serial->isOpen();
#else
    return false;
#endif
}

void CatPort::openSerial()
{
#ifdef HAVE_SERIALPORT
    if (!m_running || m_serialName.isEmpty() || serialOpen())
        return;
    if (!m_serial) {
        m_serial = new QSerialPort(this);
        connect(m_serial, &QSerialPort::readyRead, this, &CatPort::onSerialData);
        // A USB serial adapter unplugged, or the virtual pair removed: close and
        // wait for it to come back. Deferred, not inside the device's signal.
        connect(m_serial, &QSerialPort::errorOccurred, this,
                [this](QSerialPort::SerialPortError e) {
                    if (e != QSerialPort::ResourceError || !serialOpen()) return;
                    const QString why = m_serial->errorString();
                    QTimer::singleShot(0, this, [this, why] {
                        if (!serialOpen()) return;
                        qCWarning(lcCat) << "CatPort: serial" << m_serialName << "lost:" << why;
                        closeSerial();
                        m_serialError = why;
                        emit serialStateChanged(false, m_serialError);
                        if (m_running) m_serialRetry->start();
                    });
                });
    }
    if (!m_serialRetry) {
        m_serialRetry = new QTimer(this);
        m_serialRetry->setSingleShot(true);
        m_serialRetry->setInterval(3000);
        connect(m_serialRetry, &QTimer::timeout, this, &CatPort::openSerial);
    }

    m_serial->setPortName(m_serialName);
    m_serial->setBaudRate(m_serialBaud);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);
    if (!m_serial->open(QIODevice::ReadWrite)) {
        const QString why = m_serial->errorString();
        if (why != m_serialError) {   // log a change, not every retry
            qCWarning(lcCat) << "CatPort: cannot open serial" << m_serialName << ":" << why
                             << "(retrying every 3 s)";
            m_serialError = why;
            emit serialStateChanged(false, m_serialError);
        }
        m_serialRetry->start();
        return;
    }
    m_serial->clear();
    m_serialError.clear();
    m_serialBuffer.clear();
    if (m_dialect == CatDialect::Rigctld) {
        m_serialRigctl = new RigctlProtocol(m_model);
        m_serialRigctl->setSliceIndex(m_vfoA);
    } else {
        const bool flex = (m_dialect == CatDialect::FlexCAT || m_dialect == CatDialect::Thetis);
        m_serialSession = new SmartCatSession(m_serial, m_model, m_vfoA, m_vfoB,
                                              flex, this, /*ownsDevice=*/false);
        m_serialSession->setThetisDialect(m_dialect == CatDialect::Thetis);
    }
    qCInfo(lcCat) << "CatPort: serial" << m_serialName << "open," << m_serialBaud
                  << "baud 8N1, dialect" << static_cast<int>(m_dialect);
    emit serialStateChanged(true, {});
#endif
}

void CatPort::closeSerial()
{
#ifdef HAVE_SERIALPORT
    if (m_serialRetry) m_serialRetry->stop();
    // The session first: its destructor releases a CAT-held PTT.
    delete m_serialSession;
    m_serialSession = nullptr;
    delete m_serialRigctl;
    m_serialRigctl = nullptr;
    m_serialBuffer.clear();
    const bool wasOpen = serialOpen();
    if (m_serial && m_serial->isOpen())
        m_serial->close();
    if (wasOpen)
        qCInfo(lcCat) << "CatPort: serial" << m_serialName << "closed";
    if (!m_running) {
        m_serialError.clear();
        emit serialStateChanged(false, {});
    }
#endif
}

void CatPort::onSerialData()
{
#ifdef HAVE_SERIALPORT
    // TS-2000 / FlexCAT / Thetis sessions read the port themselves.
    if (!m_serialRigctl || !m_serial) return;
    m_serialBuffer.append(m_serial->readAll());
    while (true) {
        int nlPos = m_serialBuffer.indexOf('\n');
        if (nlPos < 0) nlPos = m_serialBuffer.indexOf('\r');
        if (nlPos < 0) break;
        const QString line = QString::fromUtf8(m_serialBuffer.left(nlPos)).trimmed();
        m_serialBuffer.remove(0, nlPos + 1);
        if (line.isEmpty()) continue;
        const QString resp = m_serialRigctl->handleLine(line);
        if (!resp.isEmpty())
            m_serial->write(resp.toUtf8());
    }
#endif
}

QString CatPort::ptyPath() const
{
#ifndef Q_OS_WIN
    if (!isRunning()) return {};
    if (!m_symlinkPath.isEmpty())
        return m_symlinkPath;
    return m_ptySlavePath;
#else
    return {};
#endif
}

// ── New TCP connection ───────────────────────────────────────────────────────

void CatPort::onNewConnection()
{
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    if (!socket) return;

    if (m_dialect == CatDialect::Rigctld) {
        RigctlClient c;
        c.socket   = socket;
        c.protocol = new RigctlProtocol(m_model);
        c.protocol->setSliceIndex(m_vfoA);
        m_rigctlClients.append(c);

        connect(socket, &QTcpSocket::readyRead,
                this, &CatPort::onRigctlData);
        connect(socket, &QTcpSocket::disconnected,
                this, &CatPort::onRigctlDisconnected);

        qCInfo(lcCat) << "CatPort: rigctld client connected on port" << port();
    } else {
        bool flex = (m_dialect == CatDialect::FlexCAT || m_dialect == CatDialect::Thetis);
        auto* session = new SmartCatSession(socket, m_model,
                                            m_vfoA, m_vfoB, flex, this);
        session->setThetisDialect(m_dialect == CatDialect::Thetis);
        m_catSessions.append(session);

        connect(session, &SmartCatSession::sessionEnded,
                this, &CatPort::onCatSessionEnded);

        qCInfo(lcCat) << "CatPort: SmartCAT client connected on port" << port()
                      << "(flex=" << flex << ")";
    }

    emit clientCountChanged(clientCount());
}

// ── Rigctld TCP I/O ──────────────────────────────────────────────────────────

void CatPort::onRigctlData()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    for (auto& c : m_rigctlClients) {
        if (c.socket != socket) continue;
        c.buffer.append(socket->readAll());

        while (true) {
            int nlPos = c.buffer.indexOf('\n');
            if (nlPos < 0) nlPos = c.buffer.indexOf('\r');
            if (nlPos < 0) break;

            QString line = QString::fromUtf8(c.buffer.left(nlPos)).trimmed();
            c.buffer.remove(0, nlPos + 1);
            if (line.isEmpty()) continue;

            QString resp = c.protocol->handleLine(line);
            if (!resp.isEmpty())
                socket->write(resp.toUtf8());
        }
        break;
    }
}

void CatPort::onRigctlDisconnected()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    for (int i = 0; i < m_rigctlClients.size(); ++i) {
        if (m_rigctlClients[i].socket == socket) {
            delete m_rigctlClients[i].protocol;
            m_rigctlClients.removeAt(i);
            socket->deleteLater();
            qCInfo(lcCat) << "CatPort: rigctld client disconnected";
            break;
        }
    }
    emit clientCountChanged(clientCount());
}

// ── SmartCAT session ended ───────────────────────────────────────────────────

void CatPort::onCatSessionEnded(SmartCatSession* session)
{
    m_catSessions.removeOne(session);
    session->deleteLater();
    qCInfo(lcCat) << "CatPort: SmartCAT session ended";
    emit clientCountChanged(clientCount());
}

// ── PTY ─────────────────────────────────────────────────────────────────────

void CatPort::startPty()
{
#ifndef Q_OS_WIN
    char slaveName[256] = {};
    if (openpty(&m_ptyMasterFd, &m_ptySlaveFd, slaveName, nullptr, nullptr) != 0) {
        qCWarning(lcCat) << "CatPort: openpty() failed";
        return;
    }

    m_ptySlavePath = QString::fromLocal8Bit(slaveName);

    int flags = fcntl(m_ptyMasterFd, F_GETFL);
    fcntl(m_ptyMasterFd, F_SETFL, flags | O_NONBLOCK);

    struct termios tio;
    if (tcgetattr(m_ptySlaveFd, &tio) == 0) {
        cfmakeraw(&tio);
        tio.c_cc[VMIN]  = 1;
        tio.c_cc[VTIME] = 0;
        tcsetattr(m_ptySlaveFd, TCSANOW, &tio);
    }

    if (!m_symlinkPath.isEmpty()) {
        // Atomic replace: symlink to a tmp name in the same dir, then rename().
        // rename() is atomic; avoids the TOCTOU window of unlink+symlink.
        const QFileInfo info(m_symlinkPath);
        const QString parentDir = info.absolutePath();
        if (!QDir().mkpath(parentDir))
            qCWarning(lcCat) << "CatPort: failed to mkpath" << parentDir;
        if (::chmod(parentDir.toLocal8Bit().constData(), 0700) != 0)
            qCWarning(lcCat) << "CatPort: chmod 0700 failed on" << parentDir;

        const QString tmpPath = m_symlinkPath + QStringLiteral(".tmp");
        ::unlink(tmpPath.toLocal8Bit().constData());
        if (::symlink(slaveName, tmpPath.toLocal8Bit().constData()) == 0) {
            if (::rename(tmpPath.toLocal8Bit().constData(),
                         m_symlinkPath.toLocal8Bit().constData()) != 0) {
                ::unlink(tmpPath.toLocal8Bit().constData());
                qCWarning(lcCat) << "CatPort: symlink rename failed:" << m_symlinkPath;
            }
        } else {
            qCWarning(lcCat) << "CatPort: symlink (tmp) failed:" << tmpPath;
        }
    }

    // Create the right protocol handler for this dialect
    if (m_dialect == CatDialect::Rigctld) {
        m_ptyRigctlProtocol = new RigctlProtocol(m_model);
        m_ptyRigctlProtocol->setSliceIndex(m_vfoA);
    } else {
        bool flex = (m_dialect == CatDialect::FlexCAT || m_dialect == CatDialect::Thetis);
        m_ptyCatProtocol = new SmartCatProtocol(m_model, m_vfoA, m_vfoB, flex);
        m_ptyCatProtocol->setThetisDialect(m_dialect == CatDialect::Thetis);
    }

    m_ptyNotifier = new QSocketNotifier(m_ptyMasterFd, QSocketNotifier::Read, this);
    connect(m_ptyNotifier, &QSocketNotifier::activated,
            this, &CatPort::onPtyData);

    qCInfo(lcCat) << "CatPort: PTY started on" << m_ptySlavePath
                  << "symlink:" << m_symlinkPath;
    emit ptyPathChanged(ptyPath());
#endif
}

void CatPort::stopPty()
{
#ifndef Q_OS_WIN
    if (m_ptyMasterFd < 0) return;

    delete m_ptyNotifier;
    m_ptyNotifier = nullptr;

    delete m_ptyRigctlProtocol;
    m_ptyRigctlProtocol = nullptr;
    delete m_ptyCatProtocol;
    m_ptyCatProtocol = nullptr;

    ::close(m_ptyMasterFd);
    ::close(m_ptySlaveFd);
    m_ptyMasterFd = -1;
    m_ptySlaveFd  = -1;
    m_ptyBuffer.clear();

    if (!m_symlinkPath.isEmpty())
        ::unlink(m_symlinkPath.toLocal8Bit().constData());

    m_ptySlavePath.clear();
    emit ptyPathChanged({});
    qCInfo(lcCat) << "CatPort: PTY stopped";
#endif
}

void CatPort::onPtyData()
{
#ifndef Q_OS_WIN
    char buf[4096];
    ssize_t n = ::read(m_ptyMasterFd, buf, sizeof(buf));
    if (n <= 0) return;

    m_ptyBuffer.append(buf, static_cast<int>(n));

    // Rigctld framing: newline-delimited
    if (m_dialect == CatDialect::Rigctld) {
        while (true) {
            int nlPos = m_ptyBuffer.indexOf('\n');
            if (nlPos < 0) nlPos = m_ptyBuffer.indexOf('\r');
            if (nlPos < 0) break;

            QString line = QString::fromUtf8(m_ptyBuffer.left(nlPos)).trimmed();
            m_ptyBuffer.remove(0, nlPos + 1);
            if (line.isEmpty()) continue;

            if (m_ptyRigctlProtocol) {
                QString resp = m_ptyRigctlProtocol->handleLine(line);
                if (!resp.isEmpty()) {
                    QByteArray data = resp.toUtf8();
                    if (::write(m_ptyMasterFd, data.constData(), data.size()) < 0)
                        qCWarning(lcCat) << "CatPort: PTY write failed";
                }
            }
        }
    } else {
        // SmartCAT framing: semicolon-delimited
        int pos;
        while ((pos = m_ptyBuffer.indexOf(';')) >= 0) {
            QString cmd = QString::fromUtf8(m_ptyBuffer.left(pos)).trimmed();
            m_ptyBuffer.remove(0, pos + 1);
            if (cmd.isEmpty()) continue;

            if (m_ptyCatProtocol) {
                QString resp = m_ptyCatProtocol->processCommand(cmd);
                if (!resp.isEmpty()) {
                    QByteArray data = resp.toUtf8();
                    if (::write(m_ptyMasterFd, data.constData(), data.size()) < 0)
                        qCWarning(lcCat) << "CatPort: PTY write failed";
                }
            }
        }
    }
#endif
}

} // namespace AetherSDR
