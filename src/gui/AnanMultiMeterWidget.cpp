#include "AnanMultiMeterWidget.h"

#include <QFont>
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
constexpr double kTop = 16.0;                  // above the first row
constexpr double kBottom = 8.0;                // below the last row
// Build 736: five rows (no Id / Vd) take the room of seven -- everything in
// a row is 1.3 x the build 735 size.
constexpr double kScaleX0 = 60.0;              // where the scales start
constexpr double kScaleX1 = kW - 68.0;         // ... and end
constexpr double kNamePx = 13.0, kMarkPx = 10.0, kValuePx = 13.0;
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
double sPosition(double dbm)
{
    if (dbm <= -127.0)
        return 0.0;
    if (dbm <= -73.0)
        return (dbm + 127.0) / 54.0 * 0.6;
    return std::min(1.0, 0.6 + (dbm + 73.0) / 60.0 * 0.4);
}

// SWR: 1 at the start, infinity at the end; 2:1 a little past the middle.
double swrPosition(double swr)
{
    return clamp01((1.0 - 1.0 / std::max(1.0, swr)) / 0.9);
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
void AnanMultiMeterWidget::setAlcGain(float db) { if (std::isfinite(db)) m_alcGainDb = db; }

void AnanMultiMeterWidget::setTransmitting(bool transmitting)
{
    if (m_transmitting == transmitting)
        return;
    m_transmitting = transmitting;
    update();
}

bool AnanMultiMeterWidget::rowActive(Row row) const
{
    switch (row) {
    case Row::S:    return !m_transmitting;
    default:        return m_transmitting;
    }
}

QString AnanMultiMeterWidget::rowName(Row row)
{
    switch (row) {
    case Row::S:    return QStringLiteral("S");
    case Row::Po:   return QStringLiteral("PO");
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
    case Row::Alc:  return linear(value, -30.0, 25.0);
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
        s.marks = {{0.0, QString::fromUtf8("\xE2\x88\x92" "30")},
                   {linear(-15, -30, 25), QString::fromUtf8("\xE2\x88\x92" "15")},
                   {linear(0, -30, 25), QStringLiteral("0")}, {1.0, QStringLiteral("+25")}};
        s.redFrom = linear(0, -30, 25);
        break;
    case Row::Comp:
        for (int c : {0, 10, 20, 30})
            s.marks.append({linear(c, 0, 30), QString::number(c)});
        s.redFrom = linear(20, 0, 30);
        break;
    }
    return s;
}

float AnanMultiMeterWidget::reading(Row row) const
{
    switch (row) {
    case Row::S:    return m_rxDbm;
    case Row::Po:   return m_fwdWatts;
    case Row::Swr:  return m_swr;
    // ALC: the post-ALC peak plus the ALC's own gain reduction.
    case Row::Alc:  return std::max(-30.0f, m_alcDbfs) + std::max(0.0f, -m_alcGainDb);
    case Row::Comp: return std::max(0.0f, -m_alcGainDb);
    }
    return 0.0f;
}

QString AnanMultiMeterWidget::valueText(Row row, float v) const
{
    switch (row) {
    case Row::S: {
        if (v <= -73.0f) {
            const int u = std::clamp(int(std::lround((v + 127.0f) / 6.0f)), 0, 9);
            return QStringLiteral("S%1").arg(u);
        }
        return QStringLiteral("S9+%1").arg(int(std::lround(v + 73.0f)));
    }
    case Row::Po:
        return v < 10.0f ? QStringLiteral("%1 W").arg(v, 0, 'f', 1)
                         : QStringLiteral("%1 W").arg(int(std::lround(v)));
    case Row::Swr:  return QString::number(std::min(v, 9.9f), 'f', 1);
    case Row::Alc: {
        const int db = int(std::lround(v));
        return (db > 0 ? QStringLiteral("+%1 dB") : QStringLiteral("%1 dB")).arg(db);
    }
    case Row::Comp: return QStringLiteral("%1 dB").arg(int(std::lround(v)));
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
    double w = std::min({area.width(), area.height() / kAspect, kW});
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
                            .arg(m_maxPower).arg(int(m_transmitting));
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

    // Background.
    if (m_face == Face::Light && !m_paper.isNull()) {
        p.drawPixmap(meter, m_paper, QRectF(m_paper.rect()));
    } else {
        QLinearGradient g(meter.topLeft(), meter.bottomLeft());
        g.setColorAt(0.0, QColor(0x1b, 0x23, 0x2b));
        g.setColorAt(1.0, QColor(0x10, 0x16, 0x1c));
        p.fillRect(meter, g);
    }
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

    QFont nameFont = font();
    nameFont.setPixelSize(int(kNamePx));
    nameFont.setBold(true);
    QFont markFont = font();
    markFont.setPixelSize(int(kMarkPx));
    markFont.setBold(paper);   // build 736: heavier print on paper

    const double step = (kH - kTop - kBottom) / kRows;
    const auto X = [](double f) { return kScaleX0 + (kScaleX1 - kScaleX0) * f; };
    for (int i = 0; i < kRows; ++i) {
        const Row row = Row(i);
        const double y = kTop + (i + 0.5) * step + 4.0;   // the scale line
        const bool on = rowActive(row);
        const Scale s = scaleOf(row);

        p.setOpacity(on ? 1.0 : dim);
        p.setFont(nameFont);
        p.setPen(on ? c.accent : c.sub);
        p.drawText(QPointF(8.0, y + 4.5), rowName(row));

        // The line: ink, red over its red part (thicker).
        const double redFrom = std::min(s.redFrom, 1.0);
        p.setPen(QPen(c.ink, 1.6, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(X(0.0), y), QPointF(X(redFrom), y));
        if (s.redFrom <= 1.0) {
            p.setPen(QPen(c.red, 3.2, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(X(redFrom), y), QPointF(X(1.0), y));
        }

        p.setFont(markFont);
        for (const Mark& m : s.marks) {
            const bool red = s.redFrom <= 1.0 && m.pos >= redFrom - 1e-6;
            p.setPen(QPen(red ? c.red : c.ink, 1.3));
            p.drawLine(QPointF(X(m.pos), y - 5.5), QPointF(X(m.pos), y));
            p.setPen(red ? c.red : c.sub);
            const QRectF tr(X(m.pos) - 24.0, y - 20.0, 48.0, 13.0);
            p.drawText(tr, Qt::AlignHCenter | Qt::AlignBottom, m.text);
        }
    }
    p.setOpacity(1.0);
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

    QFont valueFont = font();
    valueFont.setPixelSize(int(kValuePx));
    valueFont.setBold(true);
    p.setFont(valueFont);

    const double step = (kH - kTop - kBottom) / kRows;
    const auto X = [](double f) { return kScaleX0 + (kScaleX1 - kScaleX0) * f; };
    for (int i = 0; i < kRows; ++i) {
        const Row row = Row(i);
        if (!rowActive(row))
            continue;
        const double y = kTop + (i + 0.5) * step + 4.0;
        const double f = std::clamp(double(m_shown[i]), 0.0, 1.0);
        // The bar up to the pointer.
        p.fillRect(QRectF(X(0.0), y + 2.5, X(f) - X(0.0), 4.5), c.bar);
        // The pointer: a triangle under the line, its tip on the value.
        QPainterPath tri;
        tri.moveTo(X(f), y + 2.0);
        tri.lineTo(X(f) - 5.5, y + 11.5);
        tri.lineTo(X(f) + 5.5, y + 11.5);
        tri.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c.pointer);
        p.drawPath(tri);
        // The value, right.
        p.setPen(c.ink);
        p.drawText(QRectF(kScaleX1 + 4.0, y - 10.0, kW - kScaleX1 - 10.0, 18.0),
                   Qt::AlignRight | Qt::AlignVCenter, valueText(row, reading(row)));
    }
}

} // namespace AetherSDR
