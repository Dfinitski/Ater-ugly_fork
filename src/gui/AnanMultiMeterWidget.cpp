#include "AnanMultiMeterWidget.h"

#include <QFont>
#include "core/ThemeManager.h"
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// Design units: the paper background, 320 x 240.
constexpr double kW = AnanMultiMeterWidget::kMaxWidth;
constexpr double kH = AnanMultiMeterWidget::kMaxHeight;
constexpr double kAspect = kH / kW;            // 0.75
// Build 738 (operator, 25.11: values in two lines, the unit below; COMP in
// a small font; the room so freed to the scales; bigger, heavier scales and
// fonts): a narrow name column, a narrow value column, long scales.
constexpr double kTop = 6.0;                   // above the first row
constexpr double kBottom = 4.0;                // below the last row
constexpr double kScaleX0 = 52.0;              // where the scales start
constexpr double kScaleX1 = kW - 60.0;         // ... and end
constexpr double kNamePx = 15.0, kNameSmallPx = 11.0, kMarkPx = 11.0;
constexpr double kValuePx = 15.0, kUnitPx = 10.0;
constexpr double kLineW = 2.2, kRedW = 4.6, kTickLen = 7.0, kTickW = 1.8;
constexpr int kTickMs = 50;

struct Palette {
    QColor ink, sub, red, accent, pointer, bar, frame;
};

Palette paletteFor(AnanMultiMeterWidget::Face face)
{
    // Build 736: on paper, dark opaque inks (drawn in the Multiply mode).
    if (face == AnanMultiMeterWidget::Face::Light)
        return {QColor(0x14, 0x0c, 0x05), QColor(0x33, 0x24, 0x14), QColor(0x8c, 0x16, 0x10),
                QColor(0x10, 0x33, 0x66), QColor(0x0d, 0x09, 0x06), QColor(0x10, 0x33, 0x66, 120),
                QColor(0x6a, 0x4a, 0x24, 170)};
    return {QColor(0xe8, 0xee, 0xf4), QColor(0x8a, 0x9a, 0xa8), QColor(0xff, 0x5a, 0x4e),
            QColor(0x4f, 0xc3, 0xf7), QColor(0xff, 0xb3, 0x40), QColor(0x4f, 0xc3, 0xf7, 90),
            QColor(0x2c, 0x3a, 0x47)};
}

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double linear(double v, double lo, double hi) { return clamp01((v - lo) / (hi - lo)); }

// S units: S1..S9 (-121..-73 dBm, 6 dB each) over the first 60 % of the
// scale, S9 .. S9+60 dB over the rest.
double sPositionRaw(double dbm)
{
    if (dbm <= -127.0)
        return 0.0;
    if (dbm <= -73.0)
        return (dbm + 127.0) / 54.0 * 0.6;
    return 0.6 + (dbm + 73.0) / 60.0 * 0.4;
}

// Build 738 (operator, 25.11): the S line stays, its marks and figures move
// left until the S1 mark is 2 units from the line's start; the pointer
// follows the same marks. Above S9+60 the pointer runs on to the line's end.
double sPosition(double dbm)
{
    const double shift = sPositionRaw(-121.0) - 2.0 / (kScaleX1 - kScaleX0);
    return std::clamp(sPositionRaw(dbm) - shift, 0.0, 1.0);
}

// SWR: 1 at the start, infinity at the end; 2:1 a little past the middle.
double swrPosition(double swr)
{
    return clamp01((1.0 - 1.0 / std::max(1.0, swr)) / 0.9);
}

// Build 742 (operator, 25.11: "when centring a label, a minus sign counts
// with half its width"): the left x of `s` centred on `cx` that way.
double centredLeft(const QFontMetricsF& fm, const QString& s, double cx)
{
    const double w = fm.horizontalAdvance(s);
    if (!s.isEmpty() && (s.at(0) == QChar(0x2212) || s.at(0) == QLatin1Char('-'))) {
        const double sign = fm.horizontalAdvance(s.left(1));
        return cx - (w - sign / 2.0) / 2.0 - sign / 2.0;
    }
    return cx - w / 2.0;
}

// Build 743 (operator, 25.11): a scale's last figure, when it has three
// digits (150), stands where a two-digit one would -- its last digit (always
// 0) where the "50" of a centred "50" would be, the rest to the left of it.
double markLeft(const QFontMetricsF& fm, const QString& s, double cx, bool last)
{
    const bool threeDigits = last && s.size() == 3 && s.at(0).isDigit()
                             && s.at(1).isDigit() && s.at(2).isDigit();
    if (threeDigits)
        return cx + fm.horizontalAdvance(s.right(2)) / 2.0 - fm.horizontalAdvance(s);
    return centredLeft(fm, s, cx);
}

// Text drawn as outlines (build 742: even letter spacing at any scale --
// hinted glyph advances round differently per letter under the painter's
// scale, "COMP" showed uneven gaps).
void drawTextAt(QPainter& p, const QFont& font, const QColor& colour,
                double left, double baseline, const QString& s)
{
    QPainterPath path;
    path.addText(QPointF(left, baseline), font, s);
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(colour);
    p.drawPath(path);
    p.restore();
}

// Baseline that centres the font's ascent..descent on `cy`.
double centredBaseline(const QFontMetricsF& fm, double cy)
{
    return cy + (fm.ascent() - fm.descent()) / 2.0;
}

QFont meterFont(double px, bool bold)
{
    QFont f;
    f.setPixelSize(int(px));
    f.setBold(bold);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QString wattsText(double w)
{
    if (w >= 20.0)
        return QString::number(qRound(w));
    return QString::number(w, 'f', w == std::floor(w) ? 0 : 1);
}

} // namespace

AnanMultiMeterWidget::AnanMultiMeterWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("multiMeter"));
    setAccessibleName(QStringLiteral("Multi Meter"));
    m_paper = QPixmap(QStringLiteral(":/images/meter-paper-light.png"));
    setFloating(false);
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &AnanMultiMeterWidget::tick);
    m_tick.start();
}

void AnanMultiMeterWidget::setFace(Face face)
{
    if (m_face == face)
        return;
    m_face = face;
    update();
}

void AnanMultiMeterWidget::setFloating(bool floating)
{
    m_floating = floating;
    if (floating) {
        setMinimumHeight(minimumSizeHint().height());
        setMaximumHeight(QWIDGETSIZE_MAX);
    } else if (width() > 0) {
        setFixedHeight(heightForWidth(width()));
    }
    QSizePolicy policy(floating ? QSizePolicy::Expanding : QSizePolicy::Preferred,
                       floating ? QSizePolicy::Expanding : QSizePolicy::Preferred);
    policy.setHeightForWidth(!floating);
    setSizePolicy(policy);
    updateGeometry();
}

int AnanMultiMeterWidget::heightForWidth(int width) const
{
    return static_cast<int>(std::ceil(std::min(width, kMaxWidth) * kAspect));
}

QSize AnanMultiMeterWidget::sizeHint() const
{
    return {kMaxWidth, kMaxHeight};
}

QSize AnanMultiMeterWidget::minimumSizeHint() const
{
    return {200, heightForWidth(200)};
}

void AnanMultiMeterWidget::setLevel(float dbm) { if (std::isfinite(dbm)) m_rxDbm = dbm; }
void AnanMultiMeterWidget::setTxMeters(float forwardWatts, float swr)
{
    if (std::isfinite(forwardWatts))
        m_fwdWatts = std::max(0.0f, forwardWatts);
    m_swr = (std::isfinite(swr) && swr >= 1.0f) ? swr : 1.0f;
}
void AnanMultiMeterWidget::setMaxPower(float watts)
{
    if (!(watts > 0.0f) || watts == m_maxPower)
        return;
    m_maxPower = watts;
    update();
}
void AnanMultiMeterWidget::setAlcLevel(float dbfs) { if (std::isfinite(dbfs)) m_alcDbfs = dbfs; }
void AnanMultiMeterWidget::setCompression(float db) { if (std::isfinite(db)) m_compDb = std::max(0.0f, db); }

void AnanMultiMeterWidget::setShadowTxScales(bool on)
{
    if (m_shadowTx == on)
        return;
    m_shadowTx = on;
    update();
}

void AnanMultiMeterWidget::setTransmitting(bool transmitting)
{
    if (m_transmitting == transmitting)
        return;
    m_transmitting = transmitting;
    update();
}

bool AnanMultiMeterWidget::rowActive(Row row) const
{
    // Build 738 (operator, 25.11): S is live always; the transmit scales
    // are dimmed on receive (still showing their minimum).
    return row == Row::S || m_transmitting || !m_shadowTx;
}

void AnanMultiMeterWidget::setSwrValid(bool valid) { m_swrValid = valid; }

QString AnanMultiMeterWidget::rowName(Row row)
{
    switch (row) {
    case Row::S:    return QStringLiteral("S");
    case Row::Po:   return QStringLiteral("PWR");   // build 738: as SWR
    case Row::Swr:  return QStringLiteral("SWR");
    case Row::Alc:  return QStringLiteral("ALC");
    case Row::Comp: return QStringLiteral("COMP");
    }
    return {};
}

double AnanMultiMeterWidget::position(Row row, float value) const
{
    switch (row) {
    case Row::S:    return sPosition(value);
    case Row::Po: {
        // Square-root law: low powers stay readable. Top = 1.5 x maxPower
        // (Max Level when set, see MainWindow_Wiring).
        const double top = 1.5 * std::max(1.0f, m_maxPower);
        return clamp01(std::sqrt(std::max(0.0, double(value)) / top));
    }
    case Row::Swr:  return swrPosition(value);
    // Build 739 (operator: "as in the applet"): ALC as the mic applet's ALC
    // gauge, the post-ALC peak -20..0 dBFS; COMP as its Compression gauge,
    // -25..0 dB, the compression a negative offset read from the right.
    case Row::Alc:  return linear(value, -20.0, 0.0);
    // Build 742 (operator, 25.11): 0 at the left, -30 at the right, the
    // pointer running right as the compression grows.
    case Row::Comp: return linear(value, 0.0, 30.0);
    }
    return 0.0;
}

AnanMultiMeterWidget::Scale AnanMultiMeterWidget::scaleOf(Row row) const
{
    Scale s;
    switch (row) {
    case Row::S:
        for (int u : {1, 3, 5, 7, 9})
            s.marks.append({sPosition(-127.0 + 6.0 * u), QString::number(u)});
        for (int db : {20, 40, 60})
            s.marks.append({sPosition(-73.0 + db), QStringLiteral("+%1").arg(db)});
        s.redFrom = sPosition(-73.0);
        break;
    case Row::Po: {
        const double top = 1.5 * std::max(1.0f, m_maxPower);
        for (double k : {0.0, 1.0 / 15.0, 1.0 / 6.0, 1.0 / 3.0, 2.0 / 3.0, 1.0})
            s.marks.append({position(Row::Po, float(top * k)), wattsText(top * k)});
        s.redFrom = position(Row::Po, float(top / 1.2));   // as Max Level's red zone
        break;
    }
    case Row::Swr:
        s.marks = {{0.0, QStringLiteral("1")}, {swrPosition(1.5), QStringLiteral("1.5")},
                   {swrPosition(2.0), QStringLiteral("2")}, {swrPosition(3.0), QStringLiteral("3")},
                   {1.0, QString::fromUtf8("\xE2\x88\x9E")}};
        s.redFrom = swrPosition(2.0);
        break;
    case Row::Alc:
        for (int a : {-20, -15, -10, -5})
            s.marks.append({linear(a, -20, 0), QString::fromUtf8("\xE2\x88\x92%1").arg(-a)});
        s.marks.append({1.0, QStringLiteral("0")});
        s.redFrom = linear(-3, -20, 0);   // the applet's red from -3 dBFS
        break;
    case Row::Comp:
        s.marks.append({0.0, QStringLiteral("0")});
        for (int c : {5, 10, 15, 20, 25, 30})
            s.marks.append({linear(c, 0, 30), QString::fromUtf8("\xE2\x88\x92%1").arg(c)});
        break;                             // no red zone, as the applet
    }
    return s;
}

float AnanMultiMeterWidget::reading(Row row) const
{
    switch (row) {
    case Row::S:    return m_rxDbm;
    // Build 738: on receive the transmit scales read their minimum.
    case Row::Po:   return m_transmitting ? m_fwdWatts : 0.0f;
    case Row::Swr:  return (m_transmitting && m_swrValid) ? m_swr : 1.0f;
    // ALC: the post-ALC peak plus the ALC's own gain reduction.
    case Row::Alc:  return m_transmitting ? std::clamp(m_alcDbfs, -20.0f, 0.0f) : -20.0f;
    case Row::Comp: return m_transmitting ? std::min(m_compDb, 30.0f) : 0.0f;
    }
    return 0.0f;
}

QString AnanMultiMeterWidget::valueText(Row row, float v) const
{
    switch (row) {
    // Build 737 (operator, 25.11: "the S-meter must give its reading in dBm
    // -- the S units are on the scale already"). Minus sign as on the scale.
    case Row::S: {
        const int dbm = int(std::lround(v));
        return dbm < 0 ? QString::fromUtf8("\xE2\x88\x92%1 dBm").arg(-dbm)
                       : QStringLiteral("%1 dBm").arg(dbm);
    }
    case Row::Po:
        return v < 10.0f ? QStringLiteral("%1 W").arg(v, 0, 'f', 1)
                         : QStringLiteral("%1 W").arg(int(std::lround(v)));
    // Build 738: as the VFO flag's SmartMTR -- "_._" while there is no
    // measured SWR (receive, or too little power).
    case Row::Swr:  return (m_transmitting && m_swrValid)
                        ? QString::number(std::min(v, 9.9f), 'f', 1) : QStringLiteral("_._");
    case Row::Alc: {
        const int db = int(std::lround(v));
        if (db < 0)
            return QString::fromUtf8("\xE2\x88\x92%1 dBFS").arg(-db);
        return QStringLiteral("%1 dBFS").arg(db);
    }
    case Row::Comp: {
        const int db = int(std::lround(v));   // shown as on the scale: -8 dB
        return db > 0 ? QString::fromUtf8("\xE2\x88\x92%1 dB").arg(db) : QStringLiteral("0 dB");
    }
    }
    return {};
}

void AnanMultiMeterWidget::tick()
{
    if (!isVisible())
        return;
    // Quick up, slower down.
    for (Row row : {Row::S, Row::Po, Row::Swr, Row::Alc, Row::Comp}) {
        float& shown = m_shown[int(row)];
        const float target = float(position(row, reading(row)));
        shown += (target - shown) * (target > shown ? 0.55f : 0.22f);
    }
    update();
}

void AnanMultiMeterWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_staticCache = QPixmap();
    if (!m_floating) {
        const int h = heightForWidth(width());
        if (minimumHeight() != h || maximumHeight() != h)
            setFixedHeight(h);
    }
}

void AnanMultiMeterWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    render(p, QRectF(rect()));
}

QRectF AnanMultiMeterWidget::meterArea(const QRectF& area) const
{
    // The largest 4:3 rectangle that fits, at most the paper's size, centred.
    // Build 740 (operator, 25.11: "the window grows, the meter does not --
    // let it grow to twice"): floating, up to 2 x the paper's size.
    const double cap = m_floating ? 2.0 * kW : kW;
    double w = std::min({area.width(), area.height() / kAspect, cap});
    const double h = w * kAspect;
    return {area.x() + (area.width() - w) / 2.0, area.y() + (area.height() - h) / 2.0, w, h};
}

void AnanMultiMeterWidget::render(QPainter& p, const QRectF& area)
{
    const QRectF meter = meterArea(area);
    if (meter.width() < 10.0)
        return;
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const QSize px(qMax(1, qRound(meter.width() * dpr)), qMax(1, qRound(meter.height() * dpr)));
    const QString key = QStringLiteral("%1x%2/%3/%4/%5/%6")
                            .arg(px.width()).arg(px.height()).arg(dpr).arg(int(m_face))
                            .arg(m_maxPower).arg(int(m_transmitting) + 2 * int(m_shadowTx));
    if (m_staticCache.isNull() || m_staticKey != key) {
        m_staticCache = QPixmap(px);
        m_staticCache.setDevicePixelRatio(dpr);
        m_staticCache.fill(Qt::transparent);
        QPainter sp(&m_staticCache);
        drawStatic(sp, QRectF(0, 0, meter.width(), meter.height()));
        m_staticKey = key;
    }
    p.drawPixmap(meter.topLeft(), m_staticCache);
    p.save();
    drawDynamic(p, meter);
    p.restore();
}

void AnanMultiMeterWidget::drawStatic(QPainter& p, const QRectF& meter)
{
    const Palette c = paletteFor(m_face);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Background: the paper, or (build 738) the applets' own background.
    if (m_face == Face::Light && !m_paper.isNull())
        p.drawPixmap(meter, m_paper, QRectF(m_paper.rect()));
    else
        p.fillRect(meter, ThemeManager::instance().color(QStringLiteral("color.background.0")));
    p.setPen(QPen(c.frame, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(meter.adjusted(0.5, 0.5, -0.5, -0.5));

    // Everything else in design units; on paper, printed (Multiply).
    p.translate(meter.topLeft());
    p.scale(meter.width() / kW, meter.width() / kW);
    const bool paper = m_face == Face::Light;
    if (paper)
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
    const double dim = paper ? 0.55 : 0.38;   // the other direction's rows

    const QFont nameFont = meterFont(kNamePx, true);
    const QFont nameSmall = meterFont(kNameSmallPx, true);
    const QFont markFont = meterFont(kMarkPx, true);

    const double step = (kH - kTop - kBottom) / kRows;
    const auto X = [](double f) { return kScaleX0 + (kScaleX1 - kScaleX0) * f; };
    const double nameLeft = std::max(0.0, (nameColumnRight(Row::Po)
        - QFontMetricsF(nameFont).horizontalAdvance(rowName(Row::Po))) / 2.0);
    for (int i = 0; i < kRows; ++i) {
        const Row row = Row(i);
        const double y = rowLineY(i, step);
        const bool on = rowActive(row);
        const Scale s = scaleOf(row);

        p.setOpacity(on ? 1.0 : dim);
        const QFont& nf = row == Row::Comp ? nameSmall : nameFont;
        const QFontMetricsF nfm(nf);
        const QColor nameCol = on ? c.accent : c.sub;
        // Build 740 (operator, 25.11: "where the P of PWR starts -- every
        // name at that x, fixed; no more centring"): PWR as build 739 centred
        // it (between the window's edge and its first figure), the others
        // left-aligned at that x.
        // Build 741 (operator): S stays where build 739 centred it.
        const double nx = row == Row::S
            ? centredLeft(nfm, rowName(row), nameColumnRight(row) / 2.0) : nameLeft;
        drawTextAt(p, nf, nameCol, nx, centredBaseline(nfm, y), rowName(row));

        // The line: ink, red over its red part (thicker).
        const double redFrom = std::min(s.redFrom, 1.0);
        p.setPen(QPen(c.ink, kLineW, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(X(0.0), y), QPointF(X(redFrom), y));
        if (s.redFrom <= 1.0) {
            p.setPen(QPen(c.red, kRedW, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(X(redFrom), y - (kRedW - kLineW) / 2.0),
                       QPointF(X(1.0), y - (kRedW - kLineW) / 2.0));
        }

        const QFont infFont = meterFont(kMarkPx * 1.75, true);   // build 738: small "\u221E"
        for (int mi = 0; mi < s.marks.size(); ++mi) {
            const Mark& m = s.marks[mi];
            const bool red = s.redFrom <= 1.0 && m.pos >= redFrom - 1e-6;
            p.setPen(QPen(red ? c.red : c.ink, kTickW, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(X(m.pos), y - kTickLen), QPointF(X(m.pos), y));
            const bool inf = m.text == QString::fromUtf8("\xE2\x88\x9E");
            const QFont& mf = inf ? infFont : markFont;
            const QFontMetricsF mfm(mf);
            drawTextAt(p, mf, red ? c.red : c.sub,
                       markLeft(mfm, m.text, X(m.pos), mi == s.marks.size() - 1),
                       y - kTickLen - 1.0 - mfm.descent(), m.text);
        }
    }
    p.setOpacity(1.0);
}

double AnanMultiMeterWidget::nameColumnRight(Row row) const
{
    // Build 739: the left edge of the scale's first figure.
    const Scale s = scaleOf(row);
    if (s.marks.isEmpty())
        return kScaleX0;
    const Mark& m = s.marks.first();
    const double x = kScaleX0 + (kScaleX1 - kScaleX0) * m.pos;
    return std::min(kScaleX0, centredLeft(QFontMetricsF(meterFont(kMarkPx, true)), m.text, x));
}

double AnanMultiMeterWidget::valueColumnLeft(Row row) const
{
    // The right edge of the scale's last figure (it reaches past the scale's
    // end), the same fonts as drawStatic().
    const Scale s = scaleOf(row);
    double right = kScaleX1;
    if (!s.marks.isEmpty()) {
        const Mark& m = s.marks.last();
        const bool inf = m.text == QString::fromUtf8("\xE2\x88\x9E");
        const QFontMetricsF fm(meterFont(inf ? kMarkPx * 1.75 : kMarkPx, true));
        const double x = kScaleX0 + (kScaleX1 - kScaleX0) * m.pos;
        right = std::max(right, markLeft(fm, m.text, x, true) + fm.horizontalAdvance(m.text));
    }
    return right;
}

double AnanMultiMeterWidget::rowLineY(int index, double step)
{
    // The scale line sits a little below the row's middle: the labels above
    // it are taller than the pointer under it.
    return kTop + index * step + step * 0.56;
}

void AnanMultiMeterWidget::drawDynamic(QPainter& p, const QRectF& meter)
{
    const Palette c = paletteFor(m_face);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.translate(meter.topLeft());
    p.scale(meter.width() / kW, meter.width() / kW);
    if (m_face == Face::Light)
        p.setCompositionMode(QPainter::CompositionMode_Multiply);

    const QFont valueFont = meterFont(kValuePx, true);
    const QFont unitFont = meterFont(kUnitPx, true);
    const QFontMetricsF vfm(valueFont), ufm(unitFont);

    const double step = (kH - kTop - kBottom) / kRows;
    const auto X = [](double f) { return kScaleX0 + (kScaleX1 - kScaleX0) * f; };
    for (int i = 0; i < kRows; ++i) {
        const Row row = Row(i);
        // Build 738: a dimmed row still shows its pointer and value.
        p.setOpacity(rowActive(row) ? 1.0 : (m_face == Face::Light ? 0.55 : 0.38));
        const double y = rowLineY(i, step);
        const double f = std::clamp(double(m_shown[i]), 0.0, 1.0);
        // The bar up to the pointer.
        const double barFrom = scaleOf(row).reversed ? X(1.0) : X(0.0);   // build 739
        p.fillRect(QRectF(std::min(barFrom, X(f)), y + kLineW / 2.0 + 1.0,
                          std::abs(X(f) - barFrom), 6.0), c.bar);
        // The pointer: a triangle under the line, its tip on the value.
        QPainterPath tri;
        tri.moveTo(X(f), y + 1.0);
        tri.lineTo(X(f) - 7.0, y + 14.0);
        tri.lineTo(X(f) + 7.0, y + 14.0);
        tri.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c.pointer);
        p.drawPath(tri);
        // The value at the right in two lines (build 738): the number, the
        // unit under it.
        const QString text = valueText(row, reading(row));
        const int sp = text.lastIndexOf(QLatin1Char(' '));
        const QString number = sp > 0 ? text.left(sp) : text;
        const QString unit = sp > 0 ? text.mid(sp + 1) : QString();
        // Build 738: each line centred between the scale's last figure and
        // the window's edge.
        const double left = valueColumnLeft(row);
        const double cx = (left + kW) / 2.0;
        drawTextAt(p, valueFont, c.ink, centredLeft(vfm, number, cx),
                   centredBaseline(vfm, y - 8.0), number);
        if (!unit.isEmpty())
            drawTextAt(p, unitFont, c.sub, centredLeft(ufm, unit, cx),
                       centredBaseline(ufm, y + 7.0), unit);
    }
}

} // namespace AetherSDR
