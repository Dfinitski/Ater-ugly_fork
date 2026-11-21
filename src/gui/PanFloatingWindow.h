#pragma once

#include <QWidget>

class QVBoxLayout;

namespace AetherSDR {

class PanadapterApplet;

// Top-level window hosting a detached panadapter. Created by
// PanadapterStack::floatPanadapter(), destroyed on dock.
class PanFloatingWindow : public QWidget {
    Q_OBJECT

public:
    explicit PanFloatingWindow(QWidget* parent = nullptr);

    void adoptApplet(PanadapterApplet* applet);
    PanadapterApplet* takeApplet();
    PanadapterApplet* applet() const { return m_applet; }
    QString panId() const;

    void saveWindowGeometry();
    void restoreWindowGeometry();
    void setShuttingDown(bool on) { m_shuttingDown = on; }

    // Apply or remove Qt::FramelessWindowHint at runtime to match the
    // main-window frameless setting.  Preserves geometry and visibility.
    void setFramelessMode(bool on);

    // ANAN fork build 559 (operator, 20.11): "Always on top", from the
    // panadapter's right-click menu while it is floated. Off by default,
    // kept per panadapter ("FloatingPan_<id>_AlwaysOnTop"); a docked
    // panadapter has no such option.
    // The panadapter reaches both through the meta-object (property
    // "panAlwaysOnTop", invokable setAlwaysOnTop), so SpectrumWidget does
    // not depend on this class.
    bool alwaysOnTop() const { return m_alwaysOnTop; }
    Q_INVOKABLE void setAlwaysOnTop(bool on);   // the operator's choice: applied and saved
    void restoreAlwaysOnTop();        // the saved choice, when the window is made

signals:
    void dockRequested(const QString& panId);

protected:
    void closeEvent(QCloseEvent* ev) override;

private:
    void refreshWindowTitle();

    PanadapterApplet* m_applet{nullptr};
    QVBoxLayout* m_layout{nullptr};
    bool m_shuttingDown{false};
    bool m_alwaysOnTop{false};        // build 559
    void applyAlwaysOnTop();
};

} // namespace AetherSDR
