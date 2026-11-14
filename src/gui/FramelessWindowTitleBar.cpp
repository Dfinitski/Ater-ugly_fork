#include "FramelessWindowTitleBar.h"
#include "FramelessMoveHelper.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include "core/ThemeManager.h"

namespace AetherSDR {

namespace {

constexpr const char* kBarStyle =
    "QWidget { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
    "stop:0 #5a7494, stop:0.5 #384e68, stop:1 #1e2e3e); "
    "border-bottom: 1px solid #0a1a28; }";

// Build 404: the sizes follow the bar's scale (%1 = font size, px).
QString titleStyle(int px)
{
    return QStringLiteral("QLabel { background: transparent; color: #e0ecf4;"
                          " font-size: %1px; font-weight: bold; }").arg(px);
}

QString buttonStyle(int px)
{
    return QStringLiteral("QPushButton { background: transparent; border: none;"
                          " color: #c8d8e8; font-size: %1px; font-weight: bold;"
                          " padding: 0px 4px; }"
                          "QPushButton:hover { color: #ffffff; }").arg(px);
}

QString closeButtonStyle(int px)
{
    return QStringLiteral("QPushButton { background: transparent; border: none;"
                          " color: #c8d8e8; font-size: %1px; font-weight: bold;"
                          " padding: 0px 4px; }"
                          "QPushButton:hover { color: #ffffff; background: #cc2030; }").arg(px);
}

}

FramelessWindowTitleBar::FramelessWindowTitleBar(const QString& title, QWidget* parent, double scale)
    : QWidget(parent)
{
    const auto px = [scale](int base) { return static_cast<int>(base * scale + 0.5); };
    const int btn = px(16);
    theme::setContainer(this, QStringLiteral("titlebar"));
    setFixedHeight(px(18));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(kBarStyle);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(px(6), 0, px(2), 0);
    row->setSpacing(4);

    auto* grip = new QLabel(QString::fromUtf8("\xe2\x8b\xae\xe2\x8b\xae"), this);
    AetherSDR::ThemeManager::instance().applyStyleSheet(grip, QStringLiteral("QLabel { background: transparent; color: {{color.text.secondary}}; font-size: %1px; }").arg(px(10)));
    row->addWidget(grip);

    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setStyleSheet(titleStyle(px(10)));
    row->addWidget(m_titleLabel);
    row->addStretch();

    auto* minButton = new QPushButton(QString::fromUtf8("\xe2\x80\x94"), this);
    minButton->setFixedSize(btn, btn);
    minButton->setCursor(Qt::ArrowCursor);
    minButton->setStyleSheet(buttonStyle(px(11)));
    minButton->setToolTip("Minimize");
    // Title-bar buttons must never become the dialog's default button, or
    // pressing Return in a text field would trigger them (on macOS this minimized
    // the window). Keep them out of the Return/Tab focus chain entirely.
    minButton->setAutoDefault(false);
    minButton->setDefault(false);
    minButton->setFocusPolicy(Qt::NoFocus);
    connect(minButton, &QPushButton::clicked, this, [this] {
        if (auto* w = window())
            w->showMinimized();
    });
    row->addWidget(minButton);

    auto* maxButton = new QPushButton(QString::fromUtf8("\xe2\x96\xa1"), this);
    maxButton->setFixedSize(btn, btn);
    maxButton->setCursor(Qt::ArrowCursor);
    maxButton->setStyleSheet(buttonStyle(px(11)));
    maxButton->setToolTip("Maximize");
    maxButton->setAutoDefault(false);
    maxButton->setDefault(false);
    maxButton->setFocusPolicy(Qt::NoFocus);
    connect(maxButton, &QPushButton::clicked, this, [this] {
        if (auto* w = window()) {
            if (w->isMaximized()) {
                w->showNormal();
            } else {
                w->showMaximized();
            }
        }
    });
    row->addWidget(maxButton);

    auto* closeButton = new QPushButton(QString::fromUtf8("\xc3\x97"), this);
    closeButton->setFixedSize(btn, btn);
    closeButton->setCursor(Qt::ArrowCursor);
    closeButton->setStyleSheet(closeButtonStyle(px(11)));
    closeButton->setToolTip("Close");
    closeButton->setAutoDefault(false);
    closeButton->setDefault(false);
    closeButton->setFocusPolicy(Qt::NoFocus);
    connect(closeButton, &QPushButton::clicked, this, [this] {
        if (auto* w = window())
            w->close();
    });
    row->addWidget(closeButton);
}

void FramelessWindowTitleBar::setTitleText(const QString& title)
{
    if (m_titleLabel)
        m_titleLabel->setText(title);
}

void FramelessWindowTitleBar::mousePressEvent(QMouseEvent* event)
{
    if (FramelessMoveHelper::start(this, event)) {
        return;
    }
    QWidget::mousePressEvent(event);
}

void FramelessWindowTitleBar::mouseMoveEvent(QMouseEvent* event)
{
    if (FramelessMoveHelper::move(this, event)) {
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void FramelessWindowTitleBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (FramelessMoveHelper::finish(this, event)) {
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FramelessWindowTitleBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        FramelessMoveHelper::toggleMaximized(this);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace AetherSDR
