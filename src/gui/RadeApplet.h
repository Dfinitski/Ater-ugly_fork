#pragma once

#ifdef HAVE_RADE

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;

namespace AetherSDR {

// ANAN-100D port: the FDV (FreeDV) applet. Settings -- sub-mode 1600 / 700D /
// 700E / RADE and the callsign sent (8 characters, A-Z 0-9) -- kept in
// fdv::FdvState, and the status of the running modem (sync, SNR, offset, the
// callsign received), polled from FdvState while FDV runs on this radio.
class RadeApplet : public QWidget {
    Q_OBJECT
public:
    explicit RadeApplet(QWidget* parent = nullptr);

public slots:
    void setRadeActive(bool on, const QString& label = {});

    void setRadeSynced(bool synced);
    void setRadeSnr(float snrDb);
    void setRadeFreqOffset(float hz);
    void setRadeCallsign(const QString& callsign);

private:
    QLabel*  m_statusLabel{nullptr};
    QLabel*  m_snrLabel{nullptr};
    QLabel*  m_callsignLabel{nullptr};
    QLabel*  m_offsetLabel{nullptr};
    QWidget* m_dataRows{nullptr};
    QLabel*  m_inactiveLabel{nullptr};

    QString  m_modeLabel;
    bool     m_active{false};

    QComboBox* m_modeCombo{nullptr};
    QLineEdit* m_callsignEdit{nullptr};
    QLabel*    m_errorLabel{nullptr};
    bool       m_lastSync{false};
    int        m_lastCallSerial{-1};
    void pollLocalFdv();
};

} // namespace AetherSDR

#endif // HAVE_RADE
