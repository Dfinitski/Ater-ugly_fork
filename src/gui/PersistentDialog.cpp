#include "PersistentDialog.h"
#include "FramelessResizer.h"
#include "FramelessWindowTitleBar.h"
#include "core/AppSettings.h"

#include <QCloseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QColor>
#include <QLibrary>
#include <QOperatingSystemVersion>
#include "core/ThemeManager.h"
#endif

namespace AetherSDR {

PersistentDialog::PersistentDialog(const QString& title,
                                   const QString& geomKey,
                                   QWidget* parent,
                                   bool toolWindow)
    : QDialog(parent), m_geomKey(geomKey),
      m_windowType(toolWindow ? Qt::Tool : Qt::Dialog)
{
    setWindowTitle(title);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Build 404 (operator, 14.11: "the header one and a half times larger,
    // with its text and controls; rounded corners as the main window -- for
    // all such windows of the settings group"): every window on this base.
    m_titleBar = new FramelessWindowTitleBar(title, this, 1.5);
    outer->addWidget(m_titleBar);

    // Body widget is created here, but its layout is owned by the subclass:
    //   auto* root = new QVBoxLayout(bodyWidget());
    // setFramelessMode() reaches into bodyWidget()->layout() to nudge the top
    // margin when frameless chrome is on, so subclasses don't need to know
    // about the frameless toggle.
    m_body = new QWidget(this);
    outer->addWidget(m_body, 1);

    FramelessResizer::install(this);
    setFramelessMode(
        AppSettings::instance().value("FramelessWindow", "True").toString() == "True");
}

void PersistentDialog::setFramelessMode(bool on)
{
    m_framelessOn = on;

    const QRect geom = geometry();
    const bool wasVisible = isVisible();

    Qt::WindowFlags flags = (windowFlags() & ~Qt::WindowType_Mask) | m_windowType;
    flags.setFlag(Qt::FramelessWindowHint, on);
    setWindowFlags(flags);
    if (wasVisible) {
        setGeometry(geom);
    }

    if (m_titleBar) {
        m_titleBar->setVisible(on);
    }
    applyBodyLayoutMargins();
    if (wasVisible) {
        show();
    }
}

// Nudge the subclass-owned body layout: 2 px tighter at the top when frameless
// chrome is on, so the custom title bar sits flush against the content rather
// than gaining a double-margin look.  No-op when the subclass hasn't installed
// its layout yet (base-ctor call); first showEvent() re-applies once it has.
void PersistentDialog::applyBodyLayoutMargins()
{
    if (m_body && m_body->layout()) {
        m_body->layout()->setContentsMargins(
            m_framelessOn ? m_framelessBodyMargins : m_framedBodyMargins);
    }
}

void PersistentDialog::setBodyLayoutMargins(const QMargins& framed,
                                            const QMargins& frameless)
{
    m_framedBodyMargins = framed;
    m_framelessBodyMargins = frameless;
    applyBodyLayoutMargins();
}

void PersistentDialog::showEvent(QShowEvent* event)
{
    // Restore geometry on first show only.  Deferring past the constructor
    // lets subclasses call setMinimumSize() in their own ctor without the
    // just-restored geometry being clipped by a later minimum.
    if (!m_geometryRestored) {
        m_geometryRestored = true;
        restoreGeometryFromSettings();
        // The base-ctor setFramelessMode() ran before the subclass installed
        // its body layout, so the frameless top-margin nudge was a no-op.
        // Re-apply now that the layout exists.
        applyBodyLayoutMargins();
    }
    QDialog::showEvent(event);
    applyNativeCorners();   // build 404: each show -- a flags change makes a new native window
}

// Build 404: the main window has a native frame (expanded client area), so
// Windows 11 rounds its corners itself; these windows are frameless and stay
// square unless asked: DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND, and the
// 1 px border DWM then draws in the theme's background (as
// MainWindow::applyWindowsFrameColor). dwmapi.dll is resolved at run time, so
// nothing new is linked. Windows 10 and other systems: no rounding exists.
void PersistentDialog::applyNativeCorners()
{
#ifdef Q_OS_WIN
    if (!m_framelessOn || QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows11)
        return;
    using SetAttr = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static const SetAttr setAttr = reinterpret_cast<SetAttr>(
        QLibrary::resolve(QStringLiteral("dwmapi"), "DwmSetWindowAttribute"));
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!setAttr || !hwnd)
        return;
    constexpr DWORD kCornerPreference = 33;   // DWMWA_WINDOW_CORNER_PREFERENCE
    constexpr DWORD kBorderColor = 34;        // DWMWA_BORDER_COLOR
    const int round = 2;                      // DWMWCP_ROUND
    setAttr(hwnd, kCornerPreference, &round, sizeof(round));
    const QColor bg = ThemeManager::instance().color("color.background.app");
    if (bg.isValid()) {
        const COLORREF border = RGB(bg.red(), bg.green(), bg.blue());
        setAttr(hwnd, kBorderColor, &border, sizeof(border));
    }
#endif
}

void PersistentDialog::closeEvent(QCloseEvent* event)
{
    saveGeometryToSettings();
    // Move/resize saves are in-memory only; close-time save flushes to disk.
    // Matches the crash-resilient persistence contract established by
    // ProfileManagerDialog: if AetherSDR is force-quit, the last known
    // position is already in AppSettings even if the disk flush never ran.
    if (!m_geomKey.isEmpty()) {
        AppSettings::instance().save();
    }
    QDialog::closeEvent(event);
}

void PersistentDialog::moveEvent(QMoveEvent* event)
{
    QDialog::moveEvent(event);
    // Suppress saves before the first restore has run.  Native-window creation
    // on first show() delivers moveEvent/resizeEvent *before* showEvent — if
    // those saves weren't gated they would overwrite the prior session's
    // saved geometry with the default geometry, defeating persistence.
    if (m_geometryRestored && !m_restoringGeometry) saveGeometryToSettings();
}

void PersistentDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (m_geometryRestored && !m_restoringGeometry) saveGeometryToSettings();
}

// AppSettings serializes via XML strings, so window geometry round-trips
// through base64 to preserve binary data.  Matches the project convention
// used by MainWindow{Geometry,State} and ProfileManagerDialog.
void PersistentDialog::saveGeometryToSettings()
{
    if (m_geomKey.isEmpty()) return;
    AppSettings::instance().setValue(m_geomKey, saveGeometry().toBase64());
}

void PersistentDialog::restoreGeometryFromSettings()
{
    if (m_geomKey.isEmpty()) return;
    const QString geomB64 = AppSettings::instance().value(m_geomKey).toString();
    if (geomB64.isEmpty()) return;
    // Restore triggers move/resize events; flag those as restore-driven so
    // they don't immediately overwrite the value we just loaded.
    m_restoringGeometry = true;
    restoreGeometry(QByteArray::fromBase64(geomB64.toLatin1()));
    m_restoringGeometry = false;
}

} // namespace AetherSDR
