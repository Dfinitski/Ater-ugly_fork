// AetherSDR ANAN fork, build 454: the applets' pin (ContainerTitleBar, build 263)
// for the FTX and JS8 windows -- the same icon, idle in #c8d8e8, engaged in the
// emoji's own colour. There it keeps a floating applet on top; here it keeps
// the decoder window attached to the right of the main window.
#pragma once

#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QString>

namespace PinIcon {

inline QIcon icon()
{
    const QString glyph = QString::fromUtf8("\xf0\x9f\x93\x8c");   // the pin emoji
    constexpr int kPx = 32;   // drawn large, scaled into the 16 px button
    auto render = [&glyph](bool tint) {
        QPixmap pm(kPx, kPx);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        QFont f = QGuiApplication::font();
        f.setPixelSize(kPx - 6);
        p.setFont(f);
        p.drawText(QRect(0, 0, kPx, kPx), Qt::AlignCenter, glyph);
        if (tint) {
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(pm.rect(), QColor(0xc8, 0xd8, 0xe8));
        }
        p.end();
        return pm;
    };
    QIcon ic;
    ic.addPixmap(render(true), QIcon::Normal, QIcon::Off);
    ic.addPixmap(render(false), QIcon::Normal, QIcon::On);
    ic.addPixmap(render(true), QIcon::Active, QIcon::Off);
    ic.addPixmap(render(false), QIcon::Active, QIcon::On);
    return ic;
}

// A checkable 16 x 16 pin button styled as the applets' one.
inline QPushButton *makeButton()
{
    auto *b = new QPushButton;
    b->setIcon(icon());
    b->setIconSize(QSize(13, 13));
    b->setStyleSheet("QPushButton { background: transparent; border: none; padding: 0px; } "
                     "QPushButton:hover { background: rgba(255,255,255,30); border-radius: 3px; }");
    b->setFixedSize(16, 16);
    b->setCheckable(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setCursor(Qt::ArrowCursor);
    return b;
}

inline QString tip(bool attached)
{
    return attached ? QStringLiteral("Attached to the right of the main window (click: detach)")
                    : QStringLiteral("A window of its own (click: attach to the right of the main window)");
}

} // namespace PinIcon
