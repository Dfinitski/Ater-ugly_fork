#include "PanFloatingWindow.h"
#include "PanSliceTitle.h"
#include "FramelessResizer.h"
#include "PanadapterApplet.h"
#include "Theme.h"
#include "core/AppSettings.h"

#include <QCloseEvent>
#include <QWindow>
#include <QVBoxLayout>

namespace AetherSDR {

PanFloatingWindow::PanFloatingWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    const bool frameless =
        AppSettings::instance().value("FramelessWindow", "True").toString() == "True";
    Qt::WindowFlags flags = Qt::Window;
    if (frameless) flags |= Qt::FramelessWindowHint;
    // Re-apply via setWindowFlags — some platform plugins ignore the
    // constructor bitmask and need an explicit call before show().
    setWindowFlags(flags);
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_StyledBackground, true);
    applyAppTheme(this);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    FramelessResizer::install(this);
}

void PanFloatingWindow::adoptApplet(PanadapterApplet* applet)
{
    if (!applet) return;
    m_applet = applet;

    refreshWindowTitle();
    connect(m_applet, &PanadapterApplet::sliceTitleChanged,
            this, &PanFloatingWindow::refreshWindowTitle);

    // Reparent directly into this window — addWidget() calls setParent()
    // internally, so the widget goes straight from the splitter to the
    // floating window without an intermediate nullptr/top-level state.
    // This avoids corrupting the main window's NSResponder chain on macOS.
    m_layout->addWidget(m_applet, 1);

    // Show dock icon in the applet's title bar
    m_applet->setFloatingState(true);
    connect(m_applet, &PanadapterApplet::dockClicked, this, [this]() {
        emit dockRequested(panId());
    });
}

void PanFloatingWindow::refreshWindowTitle()
{
    if (!m_applet) return;
    const QString name =
        PanSliceTitle::displayName(m_applet->sliceTitle(), m_applet->panId());
    setWindowTitle(name.isEmpty() ? QStringLiteral("AetherSDR")
                                  : QString("AetherSDR — %1").arg(name));
}

QString PanFloatingWindow::panId() const
{
    return m_applet ? m_applet->panId() : QString();
}

PanadapterApplet* PanFloatingWindow::takeApplet()
{
    if (!m_applet) return nullptr;
    auto* a = m_applet;
    disconnect(a, &PanadapterApplet::sliceTitleChanged,
               this, &PanFloatingWindow::refreshWindowTitle);
    m_layout->removeWidget(a);
    a->setParent(nullptr);
    m_applet = nullptr;
    return a;
}

void PanFloatingWindow::setFramelessMode(bool on)
{
    const bool wasVisible = isVisible();
    const QRect geom = geometry();
    Qt::WindowFlags flags = windowFlags();
    if (on) {
        flags |= Qt::FramelessWindowHint;
    } else {
        flags &= ~Qt::FramelessWindowHint;
    }
    // Build 559: the window is made anew here -- with or without the
    // on-top hint as the operator has it now.
    flags.setFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
    setWindowFlags(flags);
    if (wasVisible) setGeometry(geom);
    if (wasVisible) show();
}

// Build 559. On a window that exists the hint goes to the platform window
// (QWindow::setFlag: SetWindowPos TOPMOST on Windows, _NET_WM_STATE_ABOVE on
// X11, the window level on macOS) -- QWidget::setWindowFlags would destroy
// and re-create the native window under the panadapter's GPU surface, and
// this is called from the panadapter's own menu. Before the window exists
// the widget flag is enough.
void PanFloatingWindow::applyAlwaysOnTop()
{
    if (QWindow* handle = windowHandle())
        handle->setFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
    else
        setWindowFlag(Qt::WindowStaysOnTopHint, m_alwaysOnTop);
}

void PanFloatingWindow::setAlwaysOnTop(bool on)
{
    m_alwaysOnTop = on;
    setProperty("panAlwaysOnTop", on);
    applyAlwaysOnTop();
    if (on)
        raise();
    auto& s = AppSettings::instance();
    s.setValue(QString("FloatingPan_%1_AlwaysOnTop").arg(panId()),
               on ? QStringLiteral("True") : QStringLiteral("False"));
    s.save();
}

void PanFloatingWindow::restoreAlwaysOnTop()
{
    m_alwaysOnTop = AppSettings::instance()
        .value(QString("FloatingPan_%1_AlwaysOnTop").arg(panId()), QStringLiteral("False"))
        .toString() == QLatin1String("True");
    setProperty("panAlwaysOnTop", m_alwaysOnTop);
    if (m_alwaysOnTop)
        applyAlwaysOnTop();
}

void PanFloatingWindow::closeEvent(QCloseEvent* ev)
{
    if (m_shuttingDown) {
        ev->accept();
        return;
    }
    // Don't close — dock instead
    emit dockRequested(panId());
    ev->ignore();
}

void PanFloatingWindow::saveWindowGeometry()
{
    auto& s = AppSettings::instance();
    s.setValue(QString("FloatingPan_%1_Geometry").arg(panId()),
              QString::fromLatin1(saveGeometry().toBase64()));
}

void PanFloatingWindow::restoreWindowGeometry()
{
    auto& s = AppSettings::instance();
    QString geom = s.value(QString("FloatingPan_%1_Geometry").arg(panId())).toString();
    if (!geom.isEmpty()) {
        restoreGeometry(QByteArray::fromBase64(geom.toLatin1()));
    }
}

} // namespace AetherSDR
