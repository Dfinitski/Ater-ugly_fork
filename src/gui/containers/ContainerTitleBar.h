#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

class QTimer;

namespace AetherSDR {

// Header strip that sits at the top of every ContainerWidget.  Owns
// the title text, float/dock toggle button, close (hide) button,
// and acts as a drag handle for reorder-by-drag.  18 px tall, dark
// gradient, 10 px bold title.
class ContainerTitleBar : public QWidget {
    Q_OBJECT

public:
    static constexpr int kHeight = 18;

    explicit ContainerTitleBar(const QString& title, QWidget* parent = nullptr);

    void setTitle(const QString& title);
    QString title() const;

    // Swap the float-toggle button's icon between "float" and "dock"
    // glyphs.  Called by the owning ContainerWidget whenever its dock
    // mode changes.
    void setFloatingState(bool isFloating);

    // Show/hide the close (hide) button.  Root containers typically
    // don't want a close button (hiding the whole sidebar is done
    // through the menu bar instead).
    void setCloseButtonVisible(bool visible);

    // Sync the pin button's pressed state.  The pin button is only
    // visible while the container is in floating mode (mirrors the
    // float-button visibility in setFloatingState()).
    void setAlwaysOnTopState(bool on);
    bool alwaysOnTopState() const { return m_alwaysOnTop; }

    // Canvas placement (RFC #4887 phase 3).  Visually a variant of the
    // docked state — the float/dock button reads "return to panel", the
    // close button stays, the pin stays hidden — and the mouse handlers
    // keep the DOCKED behaviour on purpose: title-bar drags still go
    // through the owner's QDrag, which is what lets a canvas item be
    // dragged to a new spot or back onto the panel with the exact
    // mechanism the panel already uses for reordering.
    void setCanvasState(bool onCanvas);

    // ANAN-100D port: continue a drag that began in the panel as a live move
    // of this (now floating) window -- the button is still held.
    void beginFloatingDragFromCursor();

signals:
    void floatToggleClicked();
    void closeClicked();
    void alwaysOnTopToggled(bool on);
    void dragStartRequested(const QPoint& globalPos);
    // ANAN-100D port: the FLOATING window's title bar is being dragged
    // (moved / released at globalPos), so the panel can offer and take a
    // dock at the drop point.
    void floatingDragMoved(const QPoint& globalPos);
    void floatingDragReleased(const QPoint& globalPos);

    // Live canvas move (RFC #4887 phase 5).  On a canvas the title bar
    // streams the gesture instead of starting a QDrag: began carries the
    // PRESS position (the gesture origin — using the threshold-crossing
    // point would make the item jump by the threshold), moved streams every
    // motion, ended fires on release.  A press that never crosses the
    // threshold emits none of these, so a plain click still just raises.
    void canvasDragBegan(const QPoint& globalPos);
    void canvasDragMoved(const QPoint& globalPos);
    void canvasDragEnded(const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;

private:
    QLabel*      m_titleLabel{nullptr};
    QPushButton* m_pinBtn{nullptr};
    QPushButton* m_floatBtn{nullptr};
    QPushButton* m_closeBtn{nullptr};
    QPoint       m_pressPos;
    bool         m_pressed{false};
    bool         m_closeAllowed{true};   // false = explicitly disabled (sidebar)
    bool         m_isFloating{false};
    bool         m_onCanvas{false};
    bool         m_canvasDragging{false};
    bool         m_floatDragging{false};   // ANAN-100D port
    // Live tear-off (beginFloatingDragFromCursor): polled cursor follow.
    QTimer*      m_liveTimer{nullptr};
    QPoint       m_liveOffset;
    QPoint       m_liveLast;
    void liveDragTick();
    bool liveDragActive() const;
    bool         m_floatDragMoved{false};
    bool         m_alwaysOnTop{false};
};

} // namespace AetherSDR
