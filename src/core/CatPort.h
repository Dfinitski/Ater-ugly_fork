#pragma once

#include <QObject>
#include <QList>
#include <QString>

class QTcpServer;
class QTcpSocket;
class QSocketNotifier;
class QSerialPort;
class QTimer;

namespace AetherSDR {

class RadioModel;
class RigctlProtocol;
class SmartCatProtocol;
class SmartCatSession;

enum class CatDialect {
    Rigctld,   // Hamlib rigctld protocol (newline-framed)
    TS2000,    // Kenwood TS-2000 CAT, no Flex ZZ extensions
    FlexCAT,   // Kenwood TS-2000 + FlexRadio ZZ extensions
    Thetis     // Kenwood TS-2000 + Thetis / PowerSDR ZZ set (build 192)
};

// Settings key <-> dialect ("Rigctld", "TS2000", "FlexCAT", "Thetis").
inline CatDialect catDialectFromKey(const QString& key)
{
    if (key == QLatin1String("FlexCAT")) return CatDialect::FlexCAT;
    if (key == QLatin1String("TS2000"))  return CatDialect::TS2000;
    if (key == QLatin1String("Thetis"))  return CatDialect::Thetis;
    return CatDialect::Rigctld;
}
inline QString catDialectKey(CatDialect d)
{
    switch (d) {
    case CatDialect::TS2000:  return QStringLiteral("TS2000");
    case CatDialect::FlexCAT: return QStringLiteral("FlexCAT");
    case CatDialect::Thetis:  return QStringLiteral("Thetis");
    default:                  return QStringLiteral("Rigctld");
    }
}

// Whether a dialect has a configurable VFO B. rigctld (Hamlib) is single-VFO per
// port — RigctlProtocol has no VFO B input; split is create-on-demand. The
// SmartSDR dialects (TS-2000 / FlexCAT) and Thetis support a configured VFO B (dual-VFO).
// Single definition so the config UI — and any future config surface — agrees.
inline bool dialectSupportsVfoB(CatDialect d) { return d != CatDialect::Rigctld; }

// Unified CAT port: one TCP server + one PTY (non-Windows), any dialect.
// Replaces the separate RigctlServer / SmartCatServer / RigctlPty hierarchy.
//
// ANAN port (build 191): plus an optional serial port, chosen the way each OS
// names its ports. Windows has no PTY, and loggers there (N1MM, Log4OM, HRD,
// WSJT-X with a Kenwood rig) talk CAT to a COM port: AetherSDR opens one end
// of a virtual null-modem pair (com0com, VSPE: COM11 here, COM12 in the
// logger) or a real COM port. Linux keeps the PTY (the virtual port the
// logger opens) and can also open a real tty device (/dev/ttyUSB0 ...).
// Either way the serial stream gets the same dialect handling as a TCP client,
// AI push included; TCP is optional when a serial port is set (port 0).
//
// Dialect and VFO assignments must be set before calling start().
// They cannot be changed while the port is running (per SmartSDR for Mac UX).
class CatPort : public QObject {
    Q_OBJECT

public:
    static constexpr int kVfoNone = -1;  // VFO B = simplex

    explicit CatPort(RadioModel* model, QObject* parent = nullptr);
    ~CatPort() override;

    bool start(quint16 port);   // port 0: no TCP server (serial/PTY only)
    void stop();

    bool    isRunning() const;
    quint16 port() const;
    int     clientCount() const;
    QString ptyPath() const;  // empty on Windows or if PTY not running

    // Serial endpoint (effective only when stopped). Empty name = none.
    // Windows: "COM11"; Linux: "ttyUSB0" or a full /dev path.
    void    setSerialPort(const QString& name, int baud) { m_serialName = name; m_serialBaud = baud; }
    QString serialPortName() const { return m_serialName; }
    bool    serialOpen() const;
    QString serialError() const    { return m_serialError; }
    static bool serialSupported();  // false when built without Qt SerialPort

    // Config (effective only when stopped)
    void       setDialect(CatDialect d) { m_dialect = d; }
    CatDialect dialect() const          { return m_dialect; }
    void       setVfoA(int idx)         { m_vfoA = idx; }
    void       setVfoB(int idx)         { m_vfoB = idx; }
    int        vfoA() const             { return m_vfoA; }
    int        vfoB() const             { return m_vfoB; }

    // Per-user symlink path for the PTY slave device (GHSA-qxhr-cwrc-pvrm).
    // Mirrors RigctlPty::defaultSymlinkPath() — same platform/path logic.
    static QString defaultSymlinkPath(int portIndex);
    void    setSymlinkPath(const QString& path) { m_symlinkPath = path; }
    QString symlinkPath() const                 { return m_symlinkPath; }

signals:
    void clientCountChanged(int count);
    void ptyPathChanged(const QString& path);
    // Serial endpoint opened (error empty) or not (error = why; retried).
    void serialStateChanged(bool open, const QString& error);

private slots:
    void onNewConnection();
    // Rigctld TCP I/O
    void onRigctlData();
    void onRigctlDisconnected();
    // SmartCAT TCP
    void onCatSessionEnded(SmartCatSession* session);
    // PTY data
    void onPtyData();

private:
    void startPty();
    void stopPty();
    void openSerial();
    void closeSerial();
    void onSerialData();        // rigctld framing on the serial stream

    // Rigctld client state (inline, mirrors former RigctlServer)
    struct RigctlClient {
        QTcpSocket*     socket{nullptr};
        RigctlProtocol* protocol{nullptr};
        QByteArray      buffer;
    };

    RadioModel*             m_model;
    CatDialect              m_dialect{CatDialect::Rigctld};
    int                     m_vfoA{0};
    int                     m_vfoB{kVfoNone};
    QString                 m_symlinkPath;

    bool                    m_running{false};
    QTcpServer*             m_tcpServer{nullptr};
    QList<RigctlClient>     m_rigctlClients;
    QList<SmartCatSession*> m_catSessions;

    // Serial endpoint (both OSes; needs HAVE_SERIALPORT)
    QString                 m_serialName;
    int                     m_serialBaud{9600};
    QString                 m_serialError;
    QSerialPort*            m_serial{nullptr};
    QTimer*                 m_serialRetry{nullptr};
    SmartCatSession*        m_serialSession{nullptr};   // TS-2000 / FlexCAT / Thetis
    RigctlProtocol*         m_serialRigctl{nullptr};    // rigctld framing
    QByteArray              m_serialBuffer;
    // Build 218: bytes from the logger since the port opened, and the 15 s
    // "nothing received" check (logged at warning level: on by default).
    quint64                 m_serialRxBytes = 0;
    QTimer*                 m_serialSilence = nullptr;
    quint64                 m_ptyRxBytes = 0;   // Linux PTY: bytes from the logger (build 219)

    // PTY (non-Windows only)
#ifndef Q_OS_WIN
    int              m_ptyMasterFd{-1};
    int              m_ptySlaveFd{-1};
    QString          m_ptySlavePath;
    QSocketNotifier* m_ptyNotifier{nullptr};
    QByteArray       m_ptyBuffer;
    // Protocol handler for PTY (one of these is active, the other null)
    RigctlProtocol*  m_ptyRigctlProtocol{nullptr};
    SmartCatProtocol* m_ptyCatProtocol{nullptr};
#endif
};

} // namespace AetherSDR
