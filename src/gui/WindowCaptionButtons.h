#pragma once

#include <QAbstractButton>
#include <QVariantMap>
#include <QWidget>

class QHBoxLayout;

namespace AetherSDR {

// One caption control.  QAbstractButton so it is focusable and screen-reader
// named; the glyph is painted rather than set as text because the spec calls
// for 1 px stroked paths at a fixed 11 px, which no font guarantees.
class CaptionButton : public QAbstractButton {
    Q_OBJECT

public:
    // Minimal (ANAN fork build 465): enters / leaves minimal mode -- an arrow
    // down to the left, up to the right while the mode is on.
    enum class Role { Minimal, Minimize, MaximizeRestore, Close };

    CaptionButton(Role role, QWidget* parent = nullptr);

    Role role() const { return m_role; }

    // Restore glyph (two offset squares) instead of the maximize square.
    void setMaximized(bool maximized);
    void setMinimalActive(bool on);   // Role::Minimal: the arrow's direction

protected:
    void paintEvent(QPaintEvent* ev) override;
    void enterEvent(QEnterEvent* ev) override;
    void leaveEvent(QEvent* ev) override;
    void focusInEvent(QFocusEvent* ev) override;
    void focusOutEvent(QFocusEvent* ev) override;

private:
    bool  isHot() const { return m_hovered; }
    // "Focus-visible": the ring is drawn only when focus arrived from the
    // keyboard.  Qt hands the initial focus to the first widget in the tab
    // order, which is this cluster, so painting on bare hasFocus() opened every
    // window with a ring around a traffic light.
    bool  m_focusVisible{false};
    void  paintGlyph(QPainter& p, const QColor& color) const;

    Role         m_role;
    bool         m_maximized{false};
    bool         m_minimalActive{false};
    bool         m_hovered{false};
};

class WindowCaptionButtons : public QWidget {
    Q_OBJECT

public:
    explicit WindowCaptionButtons(QWidget* parent = nullptr);

    void setMaximized(bool maximized);
    // Build 465: the minimal-mode button's state, and the buttons' width and
    // spacing (the main title bar packs its right-hand group tighter).
    void setMinimalActive(bool on);
    void setMinimalButtonVisible(bool visible);
    void setButtonMetrics(int width, int spacing);

    // Introspection for the automation bridge (`titlebar` model).
    QVariantMap state() const;

signals:
    void minimalRequested();   // build 465
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();

private:
    QHBoxLayout*   m_layout{nullptr};
    CaptionButton* m_minimal{nullptr};   // build 465
    CaptionButton* m_minimize{nullptr};
    CaptionButton* m_maximize{nullptr};
    CaptionButton* m_close{nullptr};
};

} // namespace AetherSDR
