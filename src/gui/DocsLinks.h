#pragma once

// Links from the app into the user documentation site, docs.aethersdr.com.
//
// The base URL is defined once here; every in-app link (Help menu, Support &
// Diagnostics, Radio Setup tooltips) builds on it. A page's path is its slug
// under docs/user/docs/ (docs/user/docs/troubleshooting.md -> /troubleshooting).
// The bundled resources/help/*.md pages are static Markdown and carry their
// docs links as literal text, so a base-URL change must also update them.

#include <QAction>
#include <QDesktopServices>
#include <QLatin1StringView>
#include <QMenu>
#include <QString>
#include <QUrl>

namespace AetherSDR::DocsLinks {

// Build 730 (operator, 25.11): no links to docs.aethersdr.com -- the same
// pages are read from this fork's repository on GitHub (docs/user/docs/*.md).
// There is no PDF manual and no Log Analyzer there: both lead to the index.
inline constexpr QLatin1StringView kBaseUrl{
    "https://github.com/Dfinitski/Ater-ugly_fork/blob/main/docs/user/docs"};

// Absolute docs URL for a page slug such as "/troubleshooting".
inline QString url(QLatin1StringView path)
{
    return QString(kBaseUrl).append(path).append(QLatin1StringView{".md"});
}

inline QString home()              { return url(QLatin1StringView{"/index"}); }
inline QString manualPdf()         { return home(); }
inline QString logAnalyzer()       { return home(); }
inline QString troubleshooting()   { return url(QLatin1StringView{"/troubleshooting"}); }
inline QString automationBridge()  { return url(QLatin1StringView{"/automation-bridge-and-mcp"}); }

inline void open(const QString& docsUrl)
{
    QDesktopServices::openUrl(QUrl(docsUrl));
}

// The documentation block at the top of the Help menu. Each item opens the
// system browser, so no "..." (that marks an item that opens a dialog).
inline void addHelpMenuActions(QMenu* menu)
{
    const auto add = [menu](const QString& text, const QString& docsUrl) {
        QAction* action = menu->addAction(text);
        action->setMenuRole(QAction::NoRole); // prevent macOS auto-reparenting (#883)
        QObject::connect(action, &QAction::triggered, menu,
                         [docsUrl] { open(docsUrl); });
    };
    add(QStringLiteral("Documentation (GitHub)"), home());
}

} // namespace AetherSDR::DocsLinks
