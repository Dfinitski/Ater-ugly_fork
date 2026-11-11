#include "ContainerTitleBar.h"
#include "gui/FramelessMoveHelper.h"

#include <QCursor>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "core/ThemeManager.h"

namespace AetherSDR {

namespace {

constexpr const char* kBarStyle =
    "QWidget { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
    "stop:0 #5a7494, stop:0.5 #384e68, stop:1 #1e2e3e); "
    "border-bottom: 1px solid #0a1a28; }";

constexpr const char* kTitleStyle =
    "QLabel { background: transparent; color: #e0ecf4; "
    "font-size: 10px; font-weight: bold; }";

constexpr const char* kBtnStyle =
    "QPushButton { background: transparent; border: none; "
    "color: #c8d8e8; font-size: 11px; font-weight: bold; "
    "padding: 0px 4px; } "
    "QPushButton:hover { color: #ffffff; }";

// Same as kBtnStyle but with an explicit "checked" colour so an
// engaged pin reads visibly different from an idle one.
constexpr const char* kPinBtnStyle =
    "QPushButton { background: transparent; border: none; "
    "color: #c8d8e8; font-size: 11px; font-weight: bold; "
    "padding: 0px 4px; } "
    "QPushButton:hover { color: #ffffff; background: rgba(255,255,255,40); border-radius: 3px; } "
    // Build 261 (operator, 10.11: "I press the pin and nothing happens"):
    // the glyph is a colour emoji, so the old checked text colour changed
    // nothing on screen. The engaged pin now has a lit background.
    "QPushButton:checked { color: #ffd166; background: rgba(255,209,102,120); "
    "border: 1px solid #ffd166; border-radius: 3px; }";

// Float-toggle button gets a larger font-size than its siblings so
// the ⧉ glyph reads clearly at small sizes — at 11px the two-square
// detail blurs together. The button box is bumped to 18×18 to fit the
// taller glyph without clipping.
constexpr const char* kFloatBtnStyle =
    "QPushButton { background: transparent; border: none; "
    "color: #c8d8e8; font-size: 15px; font-weight: bold; "
    "padding: 0px 4px; } "
    "QPushButton:hover { color: #ffffff; }";

constexpr int kDragThresholdPx = 6;

} // namespace

ContainerTitleBar::ContainerTitleBar(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    theme::setContainer(this, QStringLiteral("titlebar"));
    setFixedHeight(kHeight);
    setCursor(Qt::ArrowCursor);   // ANAN-100D port: plain arrow, no hand
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(kBarStyle);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 2, 0);
    layout->setSpacing(4);

    // Drag grip glyph (⋮⋮) — purely decorative; actual drag events
    // come from mouseMoveEvent on the bar as a whole.
    // Addressable from the automation bridge as "<container>/containerTitleBar"
    // (#3646 pattern, same scoping as the buttons below) — the canvas
    // live-move gesture starts from THIS widget's mouse handlers, so a
    // bridge drag has to be able to land here, not on the container.
    setAccessibleName(QStringLiteral("containerTitleBar"));

    auto* grip = new QLabel(QString::fromUtf8("\xe2\x8b\xae\xe2\x8b\xae"));
    AetherSDR::ThemeManager::instance().applyStyleSheet(grip, "QLabel { background: transparent; color: {{color.text.secondary}}; font-size: 10px; }");
    layout->addWidget(grip);

    m_titleLabel = new QLabel(title);
    m_titleLabel->setStyleSheet(kTitleStyle);
    layout->addWidget(m_titleLabel);
    layout->addStretch();

    m_pinBtn = new QPushButton(QString::fromUtf8("\xf0\x9f\x93\x8c"));  // 📌
    m_pinBtn->setStyleSheet(kPinBtnStyle);
    m_pinBtn->setFixedSize(16, 16);
    m_pinBtn->setCheckable(true);
    // Accessible names so the agent automation bridge can drive the
    // title-bar controls (scoped by the container's objectName). (#3646)
    m_pinBtn->setAccessibleName(QStringLiteral("containerPin"));
    // Build 261: what it does. The floating window already stays above the
    // main window; the pin keeps it above OTHER programs too.
    m_pinBtn->setToolTip("Keep this window above other programs too (always on top)");
    m_pinBtn->setCursor(Qt::ArrowCursor);
    m_pinBtn->setVisible(false);  // shown only in float mode
    connect(m_pinBtn, &QPushButton::toggled, this,
            [this](bool on) {
                m_alwaysOnTop = on;
                emit alwaysOnTopToggled(on);
            });
    layout->addWidget(m_pinBtn);

    m_floatBtn = new QPushButton(QString::fromUtf8("\xe2\xa7\x89"));  // ⧉ two joined squares (#2430 follow-up)
    m_floatBtn->setStyleSheet(kFloatBtnStyle);
    m_floatBtn->setFixedSize(18, 18);
    m_floatBtn->setAccessibleName(QStringLiteral("containerFloatToggle"));
    m_floatBtn->setToolTip("Pop out into a floating window");
    m_floatBtn->setCursor(Qt::ArrowCursor);
    connect(m_floatBtn, &QPushButton::clicked,
            this, &ContainerTitleBar::floatToggleClicked);
    layout->addWidget(m_floatBtn);
    // ANAN-100D port (operator, 31.10): no pop-out button -- applets are
    // torn off and docked back by dragging the title bar.
    m_floatBtn->setVisible(false);

    m_closeBtn = new QPushButton(QString::fromUtf8("\xc3\x97"));  // ×
    m_closeBtn->setStyleSheet(kBtnStyle);
    m_closeBtn->setFixedSize(16, 16);
    m_closeBtn->setAccessibleName(QStringLiteral("containerClose"));
    m_closeBtn->setToolTip("Hide this container");
    m_closeBtn->setCursor(Qt::ArrowCursor);
    connect(m_closeBtn, &QPushButton::clicked,
            this, &ContainerTitleBar::closeClicked);
    layout->addWidget(m_closeBtn);
}

void ContainerTitleBar::setTitle(const QString& title)
{
    if (m_titleLabel) m_titleLabel->setText(title);
}

QString ContainerTitleBar::title() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

void ContainerTitleBar::setFloatingState(bool isFloating)
{
    m_isFloating = isFloating;
    // "↙" when floating (to dock) / "⧉" when docked (to float).
    // Single-line "⚊" was unclear; the double-square "⧉" is the
    // canonical "open in new window" affordance and reads as pop-out
    // at a glance.
    m_floatBtn->setText(isFloating
        ? QString::fromUtf8("\xe2\x86\x99")
        : QString::fromUtf8("\xe2\xa7\x89"));
    m_floatBtn->setToolTip(isFloating
        ? "Return to panel"
        : "Pop out into a floating window");
    // Build 261 (operator, 10.11): the × stays on a floating applet too and
    // closes it without dragging it back to the panel first
    // (ContainerManager::onCloseRequested).
    if (m_closeBtn) m_closeBtn->setVisible(m_closeAllowed);
    // The pin makes no sense for in-panel containers — only show it
    // when this container hosts an actual top-level window.
    if (m_pinBtn) m_pinBtn->setVisible(isFloating);
    m_floatBtn->setVisible(false);   // ANAN-100D port: drag instead
}

void ContainerTitleBar::setCanvasState(bool onCanvas)
{
    m_onCanvas = onCanvas;
    if (!onCanvas) return;   // setFloatingState() repaints the other states

    // On the canvas the one-button affordance means "back to the panel".
    // Floating from the canvas is deliberately a two-step (return, then
    // pop out) so there is exactly one reparent path per transition.
    m_floatBtn->setText(QString::fromUtf8("\xe2\x86\x99"));   // same ↙ as float-mode dock
    m_floatBtn->setToolTip(QStringLiteral("Return to panel"));
    // The canvas has no drag-to-panel path: keep its one way back.
    m_floatBtn->setVisible(true);
    if (m_closeBtn) m_closeBtn->setVisible(m_closeAllowed);
    if (m_pinBtn)   m_pinBtn->setVisible(false);
}

void ContainerTitleBar::beginFloatingDragFromCursor()
{
    // ANAN-100D port: the window just created from a torn-off tile follows
    // the cursor until the button is released. Polled, not driven by mouse
    // events: the new window has no mouse capture and no focus on Windows
    // (the button went down on the panel and the cancelled QDrag owned the
    // capture), so it would never see a move or the release.
    QWidget* win = window();
    if (!win)
        return;
    const QPoint g = QCursor::pos();
    m_liveOffset = g - win->pos();
    m_pressPos = g;
    m_liveLast = g;
    if (!m_liveTimer) {
        m_liveTimer = new QTimer(this);
        m_liveTimer->setInterval(10);
        m_liveTimer->setTimerType(Qt::PreciseTimer);
        connect(m_liveTimer, &QTimer::timeout, this, &ContainerTitleBar::liveDragTick);
    }
    m_liveTimer->start();
}

bool ContainerTitleBar::liveDragActive() const
{
    return m_liveTimer && m_liveTimer->isActive();
}

void ContainerTitleBar::liveDragTick()
{
    QWidget* win = window();
    if (!win) {
        m_liveTimer->stop();
        return;
    }
    const QPoint g = QCursor::pos();
#ifdef Q_OS_WIN
    const int vk = ::GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    const bool down = (::GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    const bool down = (QGuiApplication::mouseButtons() & Qt::LeftButton) != 0;
#endif
    if (g != m_liveLast) {
        m_liveLast = g;
        win->move(g - m_liveOffset);
        emit floatingDragMoved(g);
    }
    if (!down) {
        m_liveTimer->stop();
        // May dock and delete the floating window: emitted last.
        emit floatingDragReleased(g);
    }
}

void ContainerTitleBar::setAlwaysOnTopState(bool on)
{
    m_alwaysOnTop = on;
    if (!m_pinBtn) return;
    QSignalBlocker blocker(m_pinBtn);
    m_pinBtn->setChecked(on);
}

void ContainerTitleBar::setCloseButtonVisible(bool visible)
{
    m_closeAllowed = visible;
    if (m_closeBtn) m_closeBtn->setVisible(visible && !m_isFloating);
}

void ContainerTitleBar::mousePressEvent(QMouseEvent* ev)
{
    if (liveDragActive()) { ev->accept(); return; }
    if (ev->button() != Qt::LeftButton) { QWidget::mousePressEvent(ev); return; }
    if (m_isFloating) {
        // ANAN-100D port: a manual move (not the system move loop), so the
        // release arrives here and a drop on the panel can dock the window.
        if (QWidget* win = window()) {
            FramelessMoveHelper::Detail::startManualMove(this, win, ev);
            m_floatDragging = true;
            m_floatDragMoved = false;
            m_pressPos = ev->globalPosition().toPoint();
        }
        return;
    }
    m_pressed = true;
    m_pressPos = ev->globalPosition().toPoint();
    ev->accept();
}

void ContainerTitleBar::mouseMoveEvent(QMouseEvent* ev)
{
    if (liveDragActive()) { ev->accept(); return; }
    if (m_isFloating && FramelessMoveHelper::move(this, ev)) {
        if (m_floatDragging) {
            const QPoint g = ev->globalPosition().toPoint();
            if (!m_floatDragMoved && (g - m_pressPos).manhattanLength() >= kDragThresholdPx)
                m_floatDragMoved = true;
            if (m_floatDragMoved)
                emit floatingDragMoved(g);
        }
        return;
    }
    if (!m_pressed || !(ev->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(ev);
        return;
    }
    const QPoint g = ev->globalPosition().toPoint();

    // Canvas: a live gesture, not a QDrag.  The item follows the cursor via
    // the canvas gesture session; the ghost-drag path below never runs.
    if (m_onCanvas) {
        if (!m_canvasDragging) {
            if ((g - m_pressPos).manhattanLength() < kDragThresholdPx) return;
            m_canvasDragging = true;
            emit canvasDragBegan(m_pressPos);
        }
        emit canvasDragMoved(g);
        return;
    }

    if ((g - m_pressPos).manhattanLength() < kDragThresholdPx) return;
    // Let the owner handle the actual QDrag — Phase 1 just signals
    // that the user moved past the drag threshold.  Phases 3 / 4
    // hook reorder into this.
    emit dragStartRequested(g);
    m_pressed = false;
}

void ContainerTitleBar::mouseReleaseEvent(QMouseEvent* ev)
{
    if (liveDragActive()) { ev->accept(); return; }   // liveDragTick sees the release
    if (m_isFloating) {
        FramelessMoveHelper::finish(this, ev);
        if (m_floatDragging) {
            m_floatDragging = false;
            // A click without a drag is not a drop. May dock and delete the
            // floating window: emitted last.
            if (m_floatDragMoved)
                emit floatingDragReleased(ev->globalPosition().toPoint());
        }
        return;
    }
    if (m_canvasDragging) {
        m_canvasDragging = false;
        m_pressed = false;
        emit canvasDragEnded(ev->globalPosition().toPoint());
        ev->accept();
        return;
    }
    QWidget::mouseReleaseEvent(ev);
    if (ev->button() == Qt::LeftButton) {
        QTimer::singleShot(0, this, [this]() {
            if (!m_isFloating && m_pressed
                && !(QGuiApplication::mouseButtons() & Qt::LeftButton)) {
                m_pressed = false;
            }
        });
    }
}

} // namespace AetherSDR
