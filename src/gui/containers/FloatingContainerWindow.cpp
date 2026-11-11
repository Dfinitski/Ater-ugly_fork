#include "FloatingContainerWindow.h"

#include <QScrollArea>
#include "ContainerWidget.h"
#include "core/AppSettings.h"
#include "gui/FramelessResizer.h"
#include "gui/Theme.h"

#include <QByteArray>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>
#include <QEvent>
#include <algorithm>
#include <QTimer>

namespace AetherSDR {

namespace {

constexpr int kDefaultW = 300;

} // namespace

FloatingContainerWindow::FloatingContainerWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    const bool frameless =
        AppSettings::instance().value("FramelessWindow", "True").toString() == "True";
    Qt::WindowFlags flags = Qt::Window;
    if (frameless) flags |= Qt::FramelessWindowHint;
    // Re-apply via setWindowFlags — some platform plugins ignore the
    // constructor bitmask and need an explicit call before show().
    setWindowFlags(flags);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_QuitOnClose, false);
    setAttribute(Qt::WA_StyledBackground, true);
    applyAppTheme(this);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this,
            &FloatingContainerWindow::saveGeometryToKey);

    FramelessResizer::install(this);
}

FloatingContainerWindow::~FloatingContainerWindow() = default;

void FloatingContainerWindow::takeContainer(ContainerWidget* container)
{
    if (m_container == container) return;

    if (m_container) {
        releaseContainer();
    }
    m_container = container;
    if (m_container) {
        m_presentationConnection = connect(m_container,
            &ContainerWidget::presentationAvailabilityChanged, this,
            [this](bool) { applyPresentation(); });
        m_containerVisibilityConnection = connect(m_container,
            &ContainerWidget::visibilityChanged, this, [this](bool visible) {
                if (m_container && m_container->isPresentationManaged()) {
                    setVisible(visible);
                }
            });
        m_containerDestroyedConnection = connect(m_container, &QObject::destroyed,
            this, [this] { m_container = nullptr; });
        m_container->setParent(this);
        m_layout->addWidget(m_container, 1);
        m_container->setDockMode(ContainerWidget::DockMode::Floating);
        m_container->show();
        setWindowTitle(m_container->title());
        fitHeightToContent();
    }
    applyPresentation();
}

void FloatingContainerWindow::setVisible(bool visible)
{
    m_requestedVisible = visible;
    applyPresentation();
}

void FloatingContainerWindow::applyPresentation()
{
    QWidget::setVisible(m_requestedVisible && (!m_container
        || (m_container->isPresentationAvailable()
            && (!m_container->isPresentationManaged() || m_container->isContainerVisible()))));
}

ContainerWidget* FloatingContainerWindow::releaseContainer()
{
    ContainerWidget* c = m_container;
    if (c) {
        QObject::disconnect(m_presentationConnection);
        QObject::disconnect(m_containerVisibilityConnection);
        QObject::disconnect(m_containerDestroyedConnection);
        m_container = nullptr;
        m_layout->removeWidget(c);
        c->setParent(nullptr);
        c->setDockMode(ContainerWidget::DockMode::PanelDocked);
    }
    return c;
}

void FloatingContainerWindow::setGeometryKey(const QString& key)
{
    m_geometryKey = key;
}

void FloatingContainerWindow::restoreAndEnsureVisible(QWidget* anchor)
{
    m_restoring = true;
    bool restored = false;
    if (!m_geometryKey.isEmpty()) {
        const QByteArray geom = QByteArray::fromBase64(
            AppSettings::instance()
                .value(m_geometryKey, "").toByteArray());
        if (!geom.isEmpty() && restoreGeometry(geom)) {
            restored = true;
        }
    }
    if (!restored) {
        // Default: size to the container's natural sizeHint so we don't
        // open with dead space above and below the children.  Width
        // falls back to kDefaultW when the hint is too narrow to read.
        adjustSize();
        const QSize configured = m_container
            ? m_container->defaultFloatingSize() : QSize();
        const QSize hint = configured.isValid() ? configured : sizeHint();
        const int w = std::max(kDefaultW, hint.width());
        const int h = std::max(hint.height(), 80);  // tiny floor
        resize(w, h);
        QScreen* screen = anchor && anchor->screen()
            ? anchor->screen()
            : QGuiApplication::primaryScreen();
        if (screen) {
            const QRect g = screen->availableGeometry();
            move(g.center().x() - w / 2,
                 g.center().y() - h / 2);
        }
    } else {
        // Clamp to any visible screen — saved geometry may reference
        // a monitor that's no longer connected.
        bool onScreen = false;
        const QPoint tl = geometry().topLeft();
        for (QScreen* s : QGuiApplication::screens()) {
            if (s->availableGeometry().contains(tl)) { onScreen = true; break; }
        }
        if (!onScreen) {
            QScreen* screen = anchor && anchor->screen()
                ? anchor->screen() : QGuiApplication::primaryScreen();
            if (screen) {
                const QRect g = screen->availableGeometry();
                move(g.center().x() - width() / 2,
                     g.center().y() - height() / 2);
            }
        }
    }
    m_fitWidth = true;
    fitHeightToContent();
    // Content built while the window is shown (the CAT port table) does not
    // raise a LayoutRequest through its scroll area: fit once more after it,
    // then leave the width to the operator.
    QTimer::singleShot(0, this, [this] { fitHeightToContent(); });
    // Late layout passes of the opening still size the width; after that the
    // width is the operator's.
    QTimer::singleShot(300, this, [this] {
        fitHeightToContent();
        m_fitWidth = false;
    });
    m_restoring = false;
}

void FloatingContainerWindow::saveGeometryToKey() const
{
    if (m_geometryKey.isEmpty()) return;
    AppSettings::instance().setValue(
        m_geometryKey, saveGeometry().toBase64());
}

void FloatingContainerWindow::prepareShutdown()
{
    m_saveTimer.stop();
    saveGeometryToKey();
    m_shuttingDown = true;
    close();
}

void FloatingContainerWindow::setFramelessMode(bool on)
{
    const bool wasVisible = isVisible();
    const bool requestedVisible = m_requestedVisible;
    const QRect geom = geometry();
    Qt::WindowFlags flags = windowFlags();
    if (on) {
        flags |= Qt::FramelessWindowHint;
    } else {
        flags &= ~Qt::FramelessWindowHint;
    }
    setWindowFlags(flags);
    m_requestedVisible = requestedVisible;
    if (wasVisible) setGeometry(geom);
    if (wasVisible) show();
}

void FloatingContainerWindow::setAlwaysOnTop(bool on)
{
    if (m_alwaysOnTop == on) return;
    m_alwaysOnTop = on;
    const bool wasVisible = isVisible();
    const bool requestedVisible = m_requestedVisible;
    const QRect geom = geometry();
    Qt::WindowFlags flags = windowFlags();
    if (on) {
        flags |= Qt::WindowStaysOnTopHint;
    } else {
        flags &= ~Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    m_requestedVisible = requestedVisible;
    if (wasVisible) setGeometry(geom);
    if (wasVisible) show();
}

bool FloatingContainerWindow::event(QEvent* ev)
{
    if (ev->type() == QEvent::LayoutRequest && !m_fitPending) {
        m_fitPending = true;
        QTimer::singleShot(0, this, [this] {
            m_fitPending = false;
            fitHeightToContent();
        });
    }
    return QWidget::event(ev);
}

void FloatingContainerWindow::fitHeightToContent()
{
    // ANAN-100D port (operator, 30.10): no resizing up and down by mouse.
    // Fixed height = what the content asks for; the width stays free.
    // FramelessResizer drops the edges of an axis that cannot resize.
    if (!m_container || !m_layout)
        return;
    int h = std::max({m_layout->sizeHint().height(),
                      m_layout->minimumSize().height(), 40});
    // Build 216 (operator, 08.11: the CAT applet opened squashed to ~120 px
    // and could not be made taller): a scroll area asks for a small fixed
    // size, not for its content, so with the height fixed the content was
    // cut. Count what each visible scroll area's content needs instead,
    // capped at the screen, so every applet opens at its own size.
    // Build 219 (operator, 08.11: "open at its own size", the width too):
    // the width the content asks for, applied when the window opens.
    int w = std::max(m_layout->sizeHint().width(), m_layout->minimumSize().width());
    for (QScrollArea* sa : m_container->findChildren<QScrollArea*>()) {
        if (!sa->isVisibleTo(m_container) || !sa->widget() || !sa->widgetResizable())
            continue;
        const int frame = 2 * sa->frameWidth();
        const QSize content = sa->widget()->sizeHint();
        const QSize have = sa->sizeHint();
        if (content.height() + frame > have.height())
            h += content.height() + frame - have.height();
        if (content.width() + frame > have.width())
            w += content.width() + frame - have.width();
    }
    if (QScreen* screen = this->screen()) {
        h = std::min(h, screen->availableGeometry().height() * 9 / 10);
        w = std::min(w, screen->availableGeometry().width() * 9 / 10);
    }
    if (minimumHeight() != h || maximumHeight() != h)
        setFixedHeight(h);
    // Width: the content's at every opening (m_fitWidth, set by
    // restoreAndEnsureVisible), then free to make wider or narrower by mouse.
    // Build 262 (operator, 10.11: "TX Control shrank when torn off"): the
    // content's width is not narrower than the applet was in the panel.
    w = std::max(w, m_openingWidthFloor);
    if (m_fitWidth && width() != w)
        resize(w, height());
}

void FloatingContainerWindow::closeEvent(QCloseEvent* ev)
{
    if (m_shuttingDown) {
        ev->accept();
        return;
    }
    // Close = dock.  Manager handles reparenting via dockRequested.
    if (m_container) {
        emit dockRequested(m_container);
    }
    QWidget::closeEvent(ev);
}

void FloatingContainerWindow::moveEvent(QMoveEvent* ev)
{
    QWidget::moveEvent(ev);
    if (!m_restoring) m_saveTimer.start();
}

void FloatingContainerWindow::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (!m_restoring) m_saveTimer.start();
}

} // namespace AetherSDR
