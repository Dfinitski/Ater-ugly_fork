#include "BandApplet.h"

#include "ButtonStyles.h"
#include "core/ThemeManager.h"
#include "models/BandDefs.h"

#include <QColor>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace AetherSDR {

namespace {

// As the panadapter's Band panel (SpectrumOverlayMenu.cpp BAND_GRID,
// BAND_BTN_W / BAND_BTN_H and its 2 px margins and spacing).
constexpr int kBtnW = 48;
constexpr int kBtnH = 26;
constexpr int kSpacing = 2;
constexpr int kMargin = 2;
constexpr int kMinColumns = 3;

struct Def {
    const char* label;
    const char* band;   // the band-stack key; "" = the XVTR setup button
    double freqMhz;
    const char* mode;
    int group;
};

constexpr Def kDefs[] = {
    {"160", "160m", 1.900, "LSB", 0},   {"80", "80m", 3.800, "LSB", 0},
    {"60", "60m", 5.357, "USB", 0},     {"40", "40m", 7.200, "LSB", 0},
    {"30", "30m", 10.125, "DIGU", 0},   {"20", "20m", 14.225, "USB", 0},
    {"17", "17m", 18.130, "USB", 0},    {"15", "15m", 21.300, "USB", 0},
    {"12", "12m", 24.950, "USB", 0},    {"10", "10m", 28.400, "USB", 0},
    {"6", "6m", 50.150, "USB", 0},
    {"WWV", "WWV", 10.000, "AM", 1},    {"GEN", "GEN", 0.500, "AM", 1},
    {"2200", "2200m", 0.1375, "CW", 2}, {"630", "630m", 0.475, "CW", 2},
    {"XVTR", "", 0.0, "", 2},
};

// The HF time-signal carriers (WWV / WWVH), +-5 kHz: WWV is lit there.
constexpr double kWwvMhz[] = {2.5, 5.0, 10.0, 15.0, 20.0, 25.0};

} // namespace

BandApplet::BandApplet(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("bandApplet"));
    for (const Def& d : kDefs) {
        auto* b = new QPushButton(QString::fromLatin1(d.label), this);
        const QString band = QString::fromLatin1(d.band);
        b->setObjectName(QStringLiteral("bandApplet_") + (band.isEmpty() ? QStringLiteral("XVTR") : band));
        b->setFocusPolicy(Qt::NoFocus);
        b->setAutoDefault(false);
        if (band.isEmpty()) {
            b->setToolTip(QStringLiteral("Transverter setup"));
            b->setAccessibleName(QStringLiteral("Transverter setup"));
            connect(b, &QPushButton::clicked, this, [this] { emit xvtrSetupRequested(); });
        } else {
            b->setCheckable(true);
            b->setAccessibleName(band == QLatin1String("GEN") ? QStringLiteral("General coverage") : band);
            const double freq = d.freqMhz;
            const QString mode = QString::fromLatin1(d.mode);
            connect(b, &QPushButton::clicked, this, [this, b, band, freq, mode] {
                // The lit button follows the receiver (updateLit), not the click.
                {
                    const QSignalBlocker blocker(b);
                    b->setChecked(!b->isChecked());
                }
                emit bandSelected(band, freq, mode);
            });
        }
        m_buttons.append({b, band, d.group});
    }
    applyButtonStyle();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            &BandApplet::applyButtonStyle);
    m_poll = new QTimer(this);
    m_poll->setInterval(250);
    connect(m_poll, &QTimer::timeout, this, &BandApplet::updateLit);
    m_poll->start();
    relayout();
}

void BandApplet::applyButtonStyle()
{
    // The Band panel's style ("overlay.band"), but the lit button's frame
    // (operator, 27.11, build 861: "too aggressive -- half the thickness and
    // half the brightness"): 1 px instead of 2, its colour at half brightness.
    QString sheet = AetherSDR::ButtonStyles::sheet("overlay.band");
    static const QRegularExpression kFrame(QStringLiteral("border:\\s*2px solid \\{\\{([^}]+)\\}\\}"));
    const QRegularExpressionMatch m = kFrame.match(sheet);
    if (m.hasMatch()) {
        QColor c = ThemeManager::instance().color(this, m.captured(1));
        if (!c.isValid())
            c = QColor(0xc8, 0xd8, 0xe8);
        const QString half = QStringLiteral("rgba(%1, %2, %3, %4)")
                                 .arg(c.red() / 2).arg(c.green() / 2).arg(c.blue() / 2)
                                 .arg(c.alpha());
        sheet.replace(m.capturedStart(), m.capturedLength(),
                      QStringLiteral("border: 1px solid ") + half);
    }
    for (const Button& b : m_buttons)
        ThemeManager::instance().applyStyleSheet(b.btn, sheet);
}

void BandApplet::setFrequencySource(std::function<double()> source)
{
    m_freqSource = std::move(source);
    updateLit();
}

int BandApplet::columnsFor(int width) const
{
    const int avail = width - 2 * kMargin;
    return std::max(kMinColumns, (avail + kSpacing) / (kBtnW + kSpacing));
}

int BandApplet::place(int cols, QVector<QPoint>* cells) const
{
    // Group by group: a group continues the current row when the whole of it
    // fits there after one empty cell; otherwise it starts a new row. Inside
    // a group the buttons run left to right and wrap.
    cells->clear();
    int row = 0;
    int col = 0;
    int i = 0;
    const int n = m_buttons.size();
    bool first = true;
    while (i < n) {
        const int g = m_buttons[i].group;
        int size = 0;
        while (i + size < n && m_buttons[i + size].group == g)
            ++size;
        if (!first) {
            if (col > 0 && col + 1 + size <= cols)
                col += 1;          // the gap cell
            else if (col > 0) {
                ++row;
                col = 0;
            }
        }
        for (int k = 0; k < size; ++k) {
            if (col >= cols) {
                ++row;
                col = 0;
            }
            cells->append(QPoint(col, row));
            ++col;
        }
        i += size;
        first = false;
    }
    return n > 0 ? row + 1 : 0;
}

int BandApplet::heightForWidth(int width) const
{
    QVector<QPoint> cells;
    const int rows = place(columnsFor(width), &cells);
    return 2 * kMargin + rows * kBtnH + std::max(0, rows - 1) * kSpacing;
}

QSize BandApplet::sizeHint() const
{
    const int w = 2 * kMargin + kMinColumns * kBtnW + (kMinColumns - 1) * kSpacing;
    return {w, heightForWidth(width() > 0 ? width() : w)};
}

QSize BandApplet::minimumSizeHint() const
{
    const int w = 2 * kMargin + kMinColumns * kBtnW + (kMinColumns - 1) * kSpacing;
    return {w, heightForWidth(w)};
}

void BandApplet::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void BandApplet::relayout()
{
    const int cols = columnsFor(width() > 0 ? width() : sizeHint().width());
    QVector<QPoint> cells;
    place(cols, &cells);
    // The buttons share the width: equal columns, at least 48 px; the grid
    // is centred when the minimum columns are wider than the applet.
    const int avail = std::max(0, width() - 2 * kMargin);
    const int btnW = std::max(kBtnW, (avail - (cols - 1) * kSpacing) / cols);
    const int gridW = cols * btnW + (cols - 1) * kSpacing;
    const int x0 = std::max(kMargin, (width() - gridW) / 2);
    for (int i = 0; i < m_buttons.size() && i < cells.size(); ++i) {
        const QPoint c = cells[i];
        m_buttons[i].btn->setGeometry(x0 + c.x() * (btnW + kSpacing),
                                      kMargin + c.y() * (kBtnH + kSpacing), btnW, kBtnH);
    }
    // The applet's height follows its rows (the panel and a floating window
    // size from it).
    const int h = heightForWidth(width() > 0 ? width() : sizeHint().width());
    if (h != m_lastHeight) {
        m_lastHeight = h;
        setMinimumHeight(h);
        setMaximumHeight(h);
        updateGeometry();
    }
}

void BandApplet::updateLit()
{
    if (!isVisible() && m_freqSource)
        return;
    const double mhz = m_freqSource ? m_freqSource() : -1.0;
    QString lit;
    if (mhz >= 0.0) {
        lit = QStringLiteral("GEN");
        for (const Button& b : m_buttons) {
            if (b.band.isEmpty() || b.group == 1)
                continue;
            for (const BandDef& d : kBands) {
                if (b.band == QLatin1String(d.name) && mhz >= d.lowMhz && mhz <= d.highMhz)
                    lit = b.band;
            }
        }
        if (lit == QLatin1String("GEN")) {
            for (double w : kWwvMhz)
                if (std::abs(mhz - w) <= 0.005)
                    lit = QStringLiteral("WWV");
        }
    }
    for (const Button& b : m_buttons) {
        if (!b.btn->isCheckable())
            continue;
        const bool on = !lit.isEmpty() && b.band == lit;
        if (b.btn->isChecked() != on) {
            const QSignalBlocker blocker(b.btn);
            b.btn->setChecked(on);
        }
    }
}

} // namespace AetherSDR
