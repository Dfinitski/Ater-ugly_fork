#pragma once

#include "SmartCatProtocol.h"

#include <QObject>
#include <QIODevice>
#include <QByteArray>

namespace AetherSDR {

class RadioModel;
class SliceModel;

// Per-connection session for the SmartSDR CAT protocol.
//
// Wraps SmartCatProtocol (pure request/response) with the byte stream I/O and
// AI mode — the async push of FA/MD updates when slice state changes.
// ANAN port (build 191): the stream is any QIODevice — a TCP client socket
// (owned by the session, ends on disconnect) or the CAT serial port (owned by
// CatPort, a COM port on Windows or a tty device on Linux; no disconnect).
//
// The flexExtensions flag selects whether ZZ-prefixed commands are accepted;
// false = plain Kenwood TS-2000 dialect, true = FlexRadio CAT (default).
class SmartCatSession : public QObject {
    Q_OBJECT

public:
    // ownsDevice: the device is reparented to the session (TCP sockets).
    explicit SmartCatSession(QIODevice* io, RadioModel* model,
                             int vfoA = 0, int vfoB = -1,
                             bool flexExtensions = true,
                             QObject* parent = nullptr,
                             bool ownsDevice = true);
    ~SmartCatSession() override;

    // Thetis ZZ set instead of Flex's (build 192). Call right after construction.
    void setThetisDialect(bool on) { m_protocol.setThetisDialect(on); }

signals:
    void sessionEnded(SmartCatSession* session);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onVfoAFrequencyChanged(double mhz);
    void onVfoAModeChanged(const QString& mode);

private:
    void setAI(bool on);
    void send(const QString& data);

    SliceModel* vfoA() const;

    SmartCatProtocol m_protocol;
    QIODevice*       m_socket;
    RadioModel*      m_model;
    QByteArray       m_buffer;
    bool             m_aiEnabled{false};
};

} // namespace AetherSDR
