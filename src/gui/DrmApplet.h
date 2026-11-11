#pragma once

// ANAN port (operator, 09.11): the DRM applet. Docked it shows the receiver's
// state, the services and the text message; popped out it adds the data
// services (slideshow picture, Journaline) and the receiver settings.

#include "core/DrmStatus.h"

#include <QWidget>

class QComboBox;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QVBoxLayout;

namespace AetherSDR {

class DrmApplet : public QWidget {
    Q_OBJECT

public:
    explicit DrmApplet(QWidget* parent = nullptr);

    // Popped out (floating / canvas): the large page with the picture,
    // Journaline and settings. Docked: the compact page.
    void setFloating(bool on);

    void setStatus(const DrmStatus& status);
    // The newest slideshow picture (kept across status updates).
    void setSlide(const QImage& image, const QString& name);
    void clearSlide();
    // The slices the decoder can listen to ("A", "B", ...).
    void setSliceLetters(const QStringList& letters);
    [[nodiscard]] QString sliceLetter() const;
    // Whether this radio has a DRM decoder at all (ANAN: yes).
    void setDecoderAvailable(bool on);
    // Turn the On button off without emitting enableChanged (the decoder was
    // stopped from elsewhere: slice closed, mode changed, disconnect).
    void forceOff();

    [[nodiscard]] bool decoderEnabled() const { return m_enabled; }

    // Settings, persisted in AppSettings ("Drm*").
    struct Settings {
        int freqInterpolation = 1;   // Dream: 0 linear, 1 Wiener, 2 DFT
        int timeInterpolation = 1;   // 0 linear, 1 Wiener
        int mlcIterations = 1;       // 0..4
        bool flipSpectrum = false;
        bool recordAudio = false;
    };
    [[nodiscard]] Settings settings() const { return m_settings; }

signals:
    void enableChanged(bool on);
    void sliceChanged(const QString& letter);
    void serviceSelected(int index);
    void settingsChanged();
    void journalinePageRequested(int objectId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildCompact(QVBoxLayout* root);
    void buildLarge(QVBoxLayout* root);
    void loadSettings();
    void saveSettings();
    void refresh();
    void setEnabledUi(bool on);

    DrmStatus m_status;
    QImage m_slideImage;
    QString m_slideImageName;
    bool m_decoderAvailable = false;
    Settings m_settings;
    bool m_enabled = false;
    bool m_floating = false;

    // Header
    QPushButton* m_enableBtn = nullptr;
    QComboBox* m_sliceCombo = nullptr;
    QLabel* m_note = nullptr;

    // Status lights: IO, Time, Freq, FAC, SDC, MSC
    QLabel* m_lights[6]{};

    // Signal line
    QLabel* m_snr = nullptr;
    QLabel* m_mode = nullptr;

    // Services and text
    QPushButton* m_serviceBtns[4]{};
    QLabel* m_serviceInfo = nullptr;
    QLabel* m_label = nullptr;
    QLabel* m_text = nullptr;

    // Large page
    QFrame* m_large = nullptr;
    QLabel* m_slide = nullptr;
    QLabel* m_slideName = nullptr;
    QPlainTextEdit* m_journaline = nullptr;
    QPushButton* m_journalineHome = nullptr;
    QLabel* m_details = nullptr;
    QComboBox* m_freqIntCombo = nullptr;
    QComboBox* m_timeIntCombo = nullptr;
    QSpinBox* m_mlcSpin = nullptr;
    QCheckBox* m_flipCheck = nullptr;
};

} // namespace AetherSDR
