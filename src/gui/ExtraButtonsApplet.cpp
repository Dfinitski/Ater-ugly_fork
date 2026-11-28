#include "ExtraButtonsApplet.h"

#include "core/ThemeManager.h"

#include <QGridLayout>
#include <QPushButton>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {
constexpr int kSpacing = 2;
constexpr int kMargin = 2;
// The bottom panel's buttons are 15 px high with 9 px lettering; the panel
// applet's are 26 px high (the Band panel's, BandApplet), lettering in step.
constexpr int kPanelButtonH = 26;
constexpr int kMinButtonH = 15;
constexpr double kFontPerHeight = 9.0 / 15.0;
} // namespace

ExtraButtonsApplet::ExtraButtonsApplet(const QString& objectName, int rows, int cols,
                                       double aspect, QWidget* parent)
    : QWidget(parent)
    , m_rows(std::max(1, rows))
    , m_cols(std::max(1, cols))
    , m_aspect(aspect > 0.1 ? aspect : 1.6)
{
    setObjectName(objectName);
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    grid->setHorizontalSpacing(kSpacing);
    grid->setVerticalSpacing(kSpacing);
    for (int i = 0; i < m_rows * m_cols; ++i) {
        auto* b = new QPushButton(QStringLiteral("-"), this);
        b->setObjectName(objectName + QStringLiteral("_%1").arg(i));
        b->setCheckable(true);
        b->setAutoDefault(false);
        b->setFocusPolicy(Qt::NoFocus);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        b->setMinimumSize(1, 1);
        grid->addWidget(b, i / m_cols, i % m_cols);
        m_buttons.append(b);
    }
    for (int r = 0; r < m_rows; ++r)
        grid->setRowStretch(r, 1);
    for (int c = 0; c < m_cols; ++c)
        grid->setColumnStretch(c, 1);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    applyStyle(std::lround(kPanelButtonH * kFontPerHeight));
}

QSize ExtraButtonsApplet::sizeHint() const
{
    const int bw = static_cast<int>(std::lround(kPanelButtonH * m_aspect));
    return {2 * kMargin + m_cols * bw + (m_cols - 1) * kSpacing,
            2 * kMargin + m_rows * kPanelButtonH + (m_rows - 1) * kSpacing};
}

QSize ExtraButtonsApplet::minimumSizeHint() const
{
    const int bw = static_cast<int>(std::lround(kMinButtonH * m_aspect));
    return {2 * kMargin + m_cols * bw + (m_cols - 1) * kSpacing,
            2 * kMargin + m_rows * kMinButtonH + (m_rows - 1) * kSpacing};
}

void ExtraButtonsApplet::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // The lettering follows the buttons' height (and their width, for the
    // longest label), as the bottom panel's 9 px on 15 px.
    const int btnH = (height() - 2 * kMargin - (m_rows - 1) * kSpacing) / m_rows;
    const int btnW = (width() - 2 * kMargin - (m_cols - 1) * kSpacing) / m_cols;
    const int byH = static_cast<int>(std::lround(btnH * kFontPerHeight));
    const int byW = static_cast<int>(std::lround(btnW * kFontPerHeight / m_aspect));
    applyStyle(std::clamp(std::min(byH, byW), 8, 48));
}

void ExtraButtonsApplet::applyStyle(int fontPx)
{
    if (fontPx == m_fontPx)
        return;
    m_fontPx = fontPx;
    // The bottom panel's HAM Bands / Modes button sheet, its font size scaled.
    const QString sheet = QStringLiteral(
        "QPushButton { background: transparent; "
        "border: 1px solid {{color.background.2}}; "
        "border-radius: 2px; color: {{color.text.secondary}}; "
        "font-size: %1px; font-weight: normal; padding: 0px; }"
        "QPushButton:hover { color: {{color.text.primary}}; }"
        "QPushButton:checked { background: transparent; "
        "color: {{color.text.primary}}; "
        "border: 1px solid {{color.accent.dim}}; }"
        "QPushButton:pressed { border: 1px solid {{color.accent.dim}}; }"
        "QPushButton:disabled { color: {{color.text.disabled}}; }").arg(fontPx);
    for (QPushButton* b : m_buttons)
        ThemeManager::instance().applyStyleSheet(b, sheet);
}

} // namespace AetherSDR
