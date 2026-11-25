#include "AnanMultiMeterWidget.h"

#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QSizePolicy>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

// ── The face, in the pixels of Thetis's ananMM image (900 x 450) ───────────
// The scale arcs were measured on that image: each is an ellipse round the
// needles' pivot (the pivot below the face, as Thetis's NeedleOffset puts
// it), radii fitted to within a pixel or two along the arc. t0/t1 are the
// ellipse angles (degrees, 90 = straight up) where the arc starts and ends.
constexpr double kImgW = 900.0;
constexpr double kImgH = 450.0;
constexpr double kPivotX = 453.6;   // (0.5 + 0.004) * 900
constexpr double kPivotY = 556.2;   // (0.5 + 0.736) * 450

struct Arc { double rx, ry, t0, t1, width; };
constexpr Arc kArcS     {775.0, 482.5, 118.7, 61.5, 4.5};   // S, top line
constexpr Arc kArcBase  {772.4, 469.1, 118.3, 62.2, 8.0};   // S base / PO ticks
constexpr Arc kArcPo    {676.7, 412.7, 118.1, 62.5, 6.0};   // PO
constexpr Arc kArcSwr   {678.2, 405.8, 117.6, 63.0, 4.0};   // SWR zones (colour)
constexpr Arc kArcId    {623.7, 360.6, 116.4, 64.3, 6.5};   // Id
constexpr Arc kArcComp  {594.5, 307.2, 113.5, 67.2, 6.5};   // COMP

// Thetis calibration tables (AddAnanMM): value -> point, 0..1 of the face.
using Cal = QVector<QPair<float, QPointF>>;
const Cal kCalSignal = {
    {-127.f, {0.076, 0.31}},  {-121.f, {0.131, 0.272}}, {-115.f, {0.189, 0.254}},
    {-109.f, {0.233, 0.211}}, {-103.f, {0.284, 0.207}}, {-97.f, {0.326, 0.177}},
    {-91.f, {0.374, 0.177}},  {-85.f, {0.414, 0.151}},  {-79.f, {0.459, 0.168}},
    {-73.f, {0.501, 0.142}},  {-63.f, {0.564, 0.172}},  {-53.f, {0.63, 0.164}},
    {-43.f, {0.695, 0.203}},  {-33.f, {0.769, 0.211}},  {-23.f, {0.838, 0.272}},
    {-13.f, {0.926, 0.31}}};
const Cal kCalVolts = {{10.f, {0.559, 0.756}}, {12.5f, {0.605, 0.772}}, {15.f, {0.665, 0.784}}};
const Cal kCalAmps = {
    {0.f, {0.199, 0.576}}, {2.f, {0.27, 0.54}},    {4.f, {0.333, 0.516}},
    {6.f, {0.393, 0.504}}, {8.f, {0.448, 0.492}},  {10.f, {0.499, 0.492}},
    {12.f, {0.554, 0.488}}, {14.f, {0.608, 0.5}},  {16.f, {0.667, 0.516}},
    {18.f, {0.728, 0.54}}, {20.f, {0.799, 0.576}}};
const Cal kCalPower = {
    {0.f, {0.099, 0.352}},  {5.f, {0.164, 0.312}},  {10.f, {0.224, 0.28}},
    {25.f, {0.335, 0.236}}, {30.f, {0.367, 0.228}}, {40.f, {0.436, 0.22}},
    {50.f, {0.499, 0.212}}, {60.f, {0.559, 0.216}}, {100.f, {0.751, 0.272}},
    {150.f, {0.899, 0.352}}};
const Cal kCalSwr = {
    {1.f, {0.152, 0.468}}, {1.5f, {0.28, 0.404}}, {2.f, {0.393, 0.372}},
    {2.5f, {0.448, 0.36}}, {3.f, {0.499, 0.36}},  {10.f, {0.847, 0.476}}};
const Cal kCalComp = {
    {0.f, {0.249, 0.68}},  {5.f, {0.342, 0.64}},  {10.f, {0.425, 0.624}},
    {15.f, {0.499, 0.62}}, {20.f, {0.571, 0.628}}, {25.f, {0.656, 0.64}},
    {30.f, {0.751, 0.688}}};
const Cal kCalAlc = {{-30.f, {0.295, 0.804}}, {0.f, {0.332, 0.784}}, {25.f, {0.499, 0.756}}};

// Thetis getPerc(): linear between the two calibration points round the
// value, the end point beyond either end.
QPointF interpolate(const Cal& cal, float value)
{
    if (cal.isEmpty())
        return {};
    if (value <= cal.first().first)
        return cal.first().second;
    if (value >= cal.last().first)
        return cal.last().second;
    for (int i = 1; i < cal.size(); ++i) {
        if (value <= cal[i].first) {
            const float v0 = cal[i - 1].first, v1 = cal[i].first;
            const double f = (value - v0) / (v1 - v0);
            const QPointF a = cal[i - 1].second, b = cal[i].second;
            return a + (b - a) * f;
        }
    }
    return cal.last().second;
}

// Image pixels of a calibration point.
QPointF imgOf(const QPointF& normalised)
{
    return {normalised.x() * kImgW, normalised.y() * kImgH};
}

// Where the ray from the pivot through `through` (image pixels) meets an arc.
QPointF onArc(const Arc& arc, const QPointF& through, double radialOffset = 0.0)
{
    const double dx = through.x() - kPivotX;
    const double dy = through.y() - kPivotY;
    const double s = 1.0 / std::sqrt((dx / arc.rx) * (dx / arc.rx) + (dy / arc.ry) * (dy / arc.ry));
    const double len = std::hypot(dx, dy);
    const double k = s + (len > 0 ? radialOffset / len : 0.0);
    return {kPivotX + dx * k, kPivotY + dy * k};
}

// The ellipse angle (degrees) of a point on an arc.
double angleOn(const Arc& arc, const QPointF& point)
{
    return qRadiansToDegrees(std::atan2((kPivotY - point.y()) / arc.ry,
                                        (point.x() - kPivotX) / arc.rx));
}

double angleOfValue(const Arc& arc, const Cal& cal, float value)
{
    return angleOn(arc, onArc(arc, imgOf(interpolate(cal, value))));
}

QString tidyPower(double watts)
{
    // Thetis tidyPower(): one decimal only for a fraction at or below 8 W.
    const double remainder = watts - std::floor(watts);
    return QString::number(watts, 'f', (remainder >= 0.1 && watts <= 8.0) ? 1 : 0);
}

const QColor kRed(232, 32, 42);
const QColor kNeedleRed(233, 51, 50);
const QColor kGreen(40, 150, 56);
const QColor kYellow(236, 226, 40);

} // namespace

AnanMultiMeterWidget::AnanMultiMeterWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ananMultiMeter"));
    setAccessibleName(QStringLiteral("ANAN Multi Meter"));
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setMouseTracking(false);
    setFloating(false);
    buildNeedles();
    // Thetis: the needles' UpdateInterval, 100 ms.
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &AnanMultiMeterWidget::tick);
    m_tick.start();
}

void AnanMultiMeterWidget::buildNeedles()
{
    // AddAnanMM, needle by needle.
    m_needles.clear();
    auto add = [this](Source src, const Cal& cal, float lf, int group,
                      bool rx, bool tx, float attack, float decay,
                      bool history, int historyMs, QColor colour, QColor historyColour) {
        Needle n;
        n.source = src;
        n.calibration = cal;
        n.lengthFactor = lf;
        n.group = group;
        n.onlyRx = rx;
        n.onlyTx = tx;
        n.attack = attack;
        n.decay = decay;
        n.showHistory = history;
        n.historyMs = historyMs;
        n.colour = colour;
        n.historyColour = historyColour;
        n.value = cal.first().first;
        m_needles.push_back(n);
    };
    const QColor redHistory(233, 51, 50, 64);
    const QColor blueHistory(100, 149, 237, 64);   // CornflowerBlue, alpha 64
    add(Source::Signal, kCalSignal, 1.65f, 0, true, false, 0.8f, 0.2f, true, 4000, kNeedleRed, redHistory);
    add(Source::Volts, kCalVolts, 0.75f, 0, false, false, 0.2f, 0.2f, false, 500, {}, {});
    add(Source::Amps, kCalAmps, 1.15f, 4, false, true, 0.2f, 0.2f, false, 500, {}, {});
    add(Source::Power, kCalPower, 1.55f, 1, false, true, 0.2f, 0.1f, true, 4000, kNeedleRed, redHistory);
    add(Source::Swr, kCalSwr, 1.36f, 1, false, true, 0.2f, 0.1f, true, 4000, {}, blueHistory);
    add(Source::AlcComp, kCalComp, 0.96f, 2, false, true, 0.2f, 0.1f, false, 4000, {}, {});
    add(Source::AlcGroup, kCalAlc, 0.75f, 3, false, true, 0.2f, 0.1f, false, 4000, {}, {});
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
    return static_cast<int>(std::ceil(width * (kFaceAspect + kStripAspect)));
}

QSize AnanMultiMeterWidget::sizeHint() const
{
    return {260, heightForWidth(260)};
}

QSize AnanMultiMeterWidget::minimumSizeHint() const
{
    return {180, heightForWidth(180)};
}

void AnanMultiMeterWidget::setDisplayGroup(int group)
{
    group = std::clamp(group, 0, kDisplayGroups - 1);
    if (group == m_displayGroup)
        return;
    m_displayGroup = group;
    update();
    emit displayGroupChanged(group);
}

QString AnanMultiMeterWidget::displayGroupName(int group)
{
    switch (group) {
    case 1: return QStringLiteral("PWR/SWR");
    case 2: return QStringLiteral("Comp");
    case 3: return QStringLiteral("ALC");
    case 4: return QStringLiteral("Volts/Amps");
    default: return QStringLiteral("ALL");
    }
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
void AnanMultiMeterWidget::setSupplyVolts(float volts) { if (std::isfinite(volts)) m_volts = volts; }
void AnanMultiMeterWidget::setPaCurrent(float amps) { if (std::isfinite(amps)) m_amps = std::max(0.0f, amps); }
void AnanMultiMeterWidget::setAlcLevel(float dbfs) { if (std::isfinite(dbfs)) m_alcDbfs = dbfs; }
void AnanMultiMeterWidget::setAlcGain(float db) { if (std::isfinite(db)) m_alcGainDb = db; }

void AnanMultiMeterWidget::setTransmitting(bool transmitting)
{
    if (m_transmitting == transmitting)
        return;
    m_transmitting = transmitting;
    // Thetis ClearHistory(): the history starts again, the first 2 s of
    // readings left out of it.
    for (Needle& n : m_needles) {
        n.history.clear();
        n.ignoreNext = 2000 / m_tick.interval();
    }
    update();
}

float AnanMultiMeterWidget::reading(Source source) const
{
    switch (source) {
    case Source::Signal:   return m_rxDbm;
    case Source::Volts:    return m_volts;
    case Source::Amps:     return m_amps;
    // NormaliseTo100W: the PO scale is drawn for 100 W.
    case Source::Power:    return m_maxPower > 0.0f ? m_fwdWatts * 100.0f / m_maxPower : 0.0f;
    case Source::Swr:      return m_swr;
    // Thetis ALC_G: the ALC's gain reduction, dB (positive).
    case Source::AlcComp:  return std::max(0.0f, -m_alcGainDb);
    // Thetis ALC_GROUP: max(-30, ALC peak) + max(0, ALC_G).
    case Source::AlcGroup: return std::max(-30.0f, m_alcDbfs) + std::max(0.0f, -m_alcGainDb);
    }
    return 0.0f;
}

void AnanMultiMeterWidget::tick()
{
    // clsNeedleItem::Update(): attack towards a higher reading, decay towards
    // a lower one, a ratio per update; the history is the last historyMs.
    for (Needle& n : m_needles) {
        const float r = reading(n.source);
        const float ratio = r > n.value ? n.attack : n.decay;
        n.value = r * ratio + n.value * (1.0f - ratio);
        if (n.ignoreNext > 0) {
            --n.ignoreNext;
        } else {
            n.history.push_back(n.value);
            const size_t keep = static_cast<size_t>(std::max(1, n.historyMs / m_tick.interval()));
            while (n.history.size() > keep)
                n.history.pop_front();
        }
    }
    if (isVisible())
        update();
}

bool AnanMultiMeterWidget::needleShown(const Needle& n) const
{
    if (n.onlyRx && m_transmitting)
        return false;
    if (n.onlyTx && !m_transmitting)
        return false;
    return m_displayGroup == 0 || n.group == 0 || n.group == m_displayGroup;
}

QPointF AnanMultiMeterWidget::calibrationPoint(const Needle& n, float value) const
{
    return interpolate(n.calibration, value);
}

QRectF AnanMultiMeterWidget::meterArea(const QRectF& area) const
{
    // The meter keeps its proportions, centred in what it is given.
    const double ratio = kFaceAspect + kStripAspect;
    double w = area.width();
    double h = w * ratio;
    if (h > area.height()) {
        h = area.height();
        w = h / ratio;
    }
    return QRectF(area.x() + (area.width() - w) / 2.0, area.y() + (area.height() - h) / 2.0, w, h);
}

QRectF AnanMultiMeterWidget::boxRect(const QRectF& meter) const
{
    // Thetis's click box: x 0.76 .. 0.908 of the width, in the strip.
    const double w = meter.width();
    const double stripTop = meter.y() + w * kFaceAspect;
    const double stripH = w * kStripAspect;
    return QRectF(meter.x() + w * 0.76, stripTop + stripH * 0.16, w * 0.148, stripH * 0.68);
}

void AnanMultiMeterWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_faceCache = QPixmap();
    // Docked: as high as the panel's width asks for, also where the layout
    // round it does not ask heightForWidth().
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

void AnanMultiMeterWidget::render(QPainter& p, const QRectF& area)
{
    const QRectF meter = meterArea(area);
    const QRectF face(meter.x(), meter.y(), meter.width(), meter.width() * kFaceAspect);
    const QRectF strip(meter.x(), face.bottom(), meter.width(), meter.width() * kStripAspect);

    // The face is static: drawn once per size / face / power scale.
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const QSize px(qMax(1, qRound(face.width() * dpr)), qMax(1, qRound(face.height() * dpr)));
    if (m_faceCache.isNull() || m_faceCacheSize != px || m_faceCacheDpr != dpr
        || m_faceCacheFace != m_face || m_faceCacheMaxPower != m_maxPower) {
        m_faceCache = QPixmap(px);
        m_faceCache.setDevicePixelRatio(dpr);
        m_faceCache.fill(Qt::transparent);
        QPainter fp(&m_faceCache);
        fp.setRenderHint(QPainter::Antialiasing, true);
        fp.setRenderHint(QPainter::TextAntialiasing, true);
        drawFace(fp, QRectF(0, 0, face.width(), face.height()));
        drawPowerLabels(fp, QRectF(0, 0, face.width(), face.height()));
        m_faceCacheSize = px;
        m_faceCacheDpr = dpr;
        m_faceCacheFace = m_face;
        m_faceCacheMaxPower = m_maxPower;
    }
    p.drawPixmap(face.topLeft(), m_faceCache);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(face);   // Thetis: nothing drawn outside the needle's rect
    // Lowest Z first, as Thetis orders them (PWR 2, others 3, S 4).
    static const Source order[] = {Source::Power, Source::Volts, Source::Amps, Source::Swr,
                                   Source::AlcComp, Source::AlcGroup, Source::Signal};
    for (Source s : order) {
        for (const Needle& n : m_needles) {
            if (n.source == s && needleShown(n))
                drawNeedle(p, face, n);
        }
    }
    p.restore();

    drawStrip(p, strip);
}

void AnanMultiMeterWidget::drawNeedle(QPainter& p, const QRectF& face, const Needle& n) const
{
    // DXRenderer::renderNeedle(), Placement Bottom, NeedleOffset (0.004,
    // 0.736), RadiusRatio (1, 0.58).
    const double x = face.x(), y = face.y(), w = face.width(), h = face.height();
    const QPointF start(x + w / 2.0 + w * 0.004, y + h / 2.0 + h * 0.736);
    const double rrX = 1.0, rrY = 0.58;
    const double radiusX = (w / 2.0) * (n.lengthFactor * rrX);
    const double radiusY = (w / 2.0) * (n.lengthFactor * rrY);

    auto angleFor = [&](float value) {
        const QPointF c = calibrationPoint(n, value);
        const double eX = x + c.x() * w;
        const double eY = y + c.y() * h;
        return std::atan2((start.y() - eY) / rrY, (start.x() - eX) / rrX);
    };
    auto endFor = [&](double ang) {
        return QPointF(start.x() + std::cos(ang + M_PI) * radiusX,
                       start.y() + std::sin(ang + M_PI) * radiusY);
    };

    const QColor ink = m_face == Face::Dark ? QColor(220, 220, 220) : QColor(0, 0, 0);
    const QColor colour = n.colour.isValid() ? n.colour : ink;

    if (n.showHistory && !n.history.empty()) {
        const auto [mn, mx] = std::minmax_element(n.history.begin(), n.history.end());
        const double a0 = angleFor(*mn), a1 = angleFor(*mx);
        QPainterPath path(start);
        path.lineTo(endFor(a0));
        const int steps = 24;
        for (int i = 1; i <= steps; ++i)
            path.lineTo(endFor(a0 + (a1 - a0) * i / steps));
        path.closeSubpath();
        p.fillPath(path, n.historyColour);
    }

    const double ang = angleFor(n.value);
    const QPointF end = endFor(ang);
    const double diag = std::hypot(w, h);
    const double stroke = 3.0 * (diag / 450.0);   // ScaleStrokeWidth

    // Thetis's needle shadow: 8 strokes narrowing, shifted by the needle's
    // side of the centre.
    {
        const double fR = -((w / 2.0) - (end.x() - x)) / (w / 2.0);
        const double shift = (stroke * 1.5) * fR;
        const double total = stroke * 3.0;
        const QPointF s(start.x() + shift, start.y() + stroke * 1.5);
        const QPointF e(end.x() + shift, end.y() + stroke * 1.5);
        for (int i = 0; i < 8; ++i) {
            const double wdt = total - (i / 7.0) * total;
            if (wdt <= 0.0)
                continue;
            p.setPen(QPen(QColor(0, 0, 0, 12), wdt, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(s, e);
        }
    }
    p.setPen(QPen(colour, stroke, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(start, end);
}

// ── The face ────────────────────────────────────────────────────────────────

void AnanMultiMeterWidget::drawFace(QPainter& p, const QRectF& face)
{
    const bool dark = m_face == Face::Dark;
    const double sx = face.width() / kImgW;
    const double sy = face.height() / kImgH;
    auto map = [&](const QPointF& img) {
        return QPointF(face.x() + img.x() * sx, face.y() + img.y() * sy);
    };

    // Background.
    if (dark) {
        QLinearGradient g(face.topLeft(), face.bottomLeft());
        g.setColorAt(0.0, QColor(46, 46, 46));
        g.setColorAt(1.0, QColor(18, 18, 18));
        p.fillRect(face, g);
        // Brushed metal: fine horizontal streaks (fixed seed: the same face
        // every time).
        QRandomGenerator rng(900450);
        const int lines = static_cast<int>(face.height());
        for (int i = 0; i < lines; ++i) {
            const double yy = face.y() + i + 0.5;
            const int a = static_cast<int>(rng.bounded(14));
            p.setPen(QPen(QColor(255, 255, 255, a), 1.0));
            const double x0 = face.x() + rng.bounded(static_cast<int>(face.width() * 0.3 + 1));
            const double x1 = face.right() - rng.bounded(static_cast<int>(face.width() * 0.3 + 1));
            p.drawLine(QPointF(x0, yy), QPointF(x1, yy));
        }
    } else {
        QRadialGradient g(face.center(), face.width() * 0.62);
        g.setColorAt(0.0, QColor(244, 234, 206));
        g.setColorAt(1.0, QColor(222, 206, 166));
        p.fillRect(face, g);
    }

    const QColor ink = dark ? QColor(236, 236, 236) : QColor(10, 10, 10);
    const QColor sideInk = dark ? QColor(176, 176, 176) : QColor(10, 10, 10);

    // Polyline of an arc between two ellipse angles.
    auto arcPath = [&](const Arc& arc, double tFrom, double tTo) {
        QPainterPath path;
        const int steps = std::max(8, static_cast<int>(std::abs(tTo - tFrom) * 2));
        for (int i = 0; i <= steps; ++i) {
            const double t = qDegreesToRadians(tFrom + (tTo - tFrom) * i / steps);
            const QPointF pt = map({kPivotX + arc.rx * std::cos(t), kPivotY - arc.ry * std::sin(t)});
            if (i == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        return path;
    };
    auto strokeArc = [&](const Arc& arc, double tFrom, double tTo, const QColor& c,
                         double width, Qt::PenStyle style = Qt::SolidLine) {
        QPen pen(c, width * sy, style, Qt::FlatCap);
        if (style == Qt::CustomDashLine)
            pen.setDashPattern({2.2, 1.1});
        p.strokePath(arcPath(arc, tFrom, tTo), pen);
    };
    // A tick on an arc, along the ray of a value: from `from` to `to` image
    // pixels off the arc (outwards positive).
    auto tick = [&](const Arc& arc, const QPointF& through, double from, double to,
                    const QColor& c, double width) {
        p.setPen(QPen(c, width * sy, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(map(onArc(arc, through, from)), map(onArc(arc, through, to)));
    };
    auto fontPx = [&](double imgPx, bool bold = true) {
        QFont f(QStringLiteral("Arial"));
        f.setPixelSize(std::max(6, static_cast<int>(std::lround(imgPx * sx))));
        f.setBold(bold);
        return f;
    };
    auto label = [&](const QString& text, const QPointF& imgCentre, const QFont& f,
                     const QColor& c, Qt::Alignment align = Qt::AlignCenter) {
        p.setFont(f);
        p.setPen(c);
        const QFontMetricsF fm(f);
        const QSizeF sz(fm.horizontalAdvance(text) + 2, fm.height());
        const QPointF c0 = map(imgCentre);
        QRectF r(QPointF(c0.x() - sz.width() / 2.0, c0.y() - sz.height() / 2.0), sz);
        if (align & Qt::AlignLeft)
            r.moveLeft(c0.x());
        else if (align & Qt::AlignRight)
            r.moveRight(c0.x());
        p.drawText(r, Qt::AlignCenter, text);
    };
    auto calPt = [](const Cal& cal, float v) { return imgOf(interpolate(cal, v)); };

    // Labels sit where they are on Thetis's image (their centres, measured
    // there), turned along their scale as the image has them: the tangent of
    // an ellipse round the pivot through the label, of the scale's axis
    // ratio.
    auto rayLabel = [&](const QString& text, const QPointF& imgCentre, const QFont& f,
                        const QColor& c, double axisRatio) {
        const double dx = imgCentre.x() - kPivotX;
        const double dy = kPivotY - imgCentre.y();
        const double t = std::atan2(dy / axisRatio, dx);
        const double rot = qRadiansToDegrees(std::atan2(axisRatio * std::cos(t) * sy,
                                                        std::sin(t) * sx));
        p.save();
        p.translate(map(imgCentre));
        p.rotate(rot);
        p.setFont(f);
        p.setPen(c);
        const QFontMetricsF fm(f);
        const QSizeF sz(fm.horizontalAdvance(text) + 2, fm.height());
        p.drawText(QRectF(-sz.width() / 2.0, -sz.height() / 2.0, sz.width(), sz.height()),
                   Qt::AlignCenter, text);
        p.restore();
    };
    // A polyline given in image pixels.
    auto poly = [&](std::initializer_list<QPointF> pts, const QColor& c, double width,
                    Qt::PenCapStyle cap = Qt::FlatCap) {
        QPainterPath path;
        bool first = true;
        for (const QPointF& pt : pts) {
            if (first) path.moveTo(map(pt)); else path.lineTo(map(pt));
            first = false;
        }
        p.strokePath(path, QPen(c, width * sy, Qt::SolidLine, cap, Qt::MiterJoin));
    };

    const QFont numFont = fontPx(32);
    const QFont smallFont = fontPx(28);
    const QFont sideFont = fontPx(27);

    // ── S ──────────────────────────────────────────────────────────────────
    const double tS9 = angleOfValue(kArcS, kCalSignal, -73.f);
    strokeArc(kArcS, kArcS.t0, tS9, ink, kArcS.width);
    strokeArc(kArcS, tS9, kArcS.t1, kRed, kArcS.width);
    strokeArc(kArcBase, kArcBase.t0, kArcBase.t1, ink, kArcBase.width);
    for (int s = 0; s <= 9; ++s) {
        const QPointF thr = calPt(kCalSignal, -127.f + 6.f * s);
        const bool major = s == 0 || (s % 2) == 1;
        if (s == 9)
            tick(kArcS, thr, -32, 16, ink, 4.5);   // S9: through both lines
        else
            tick(kArcS, thr, 0, major ? 15 : 8, ink, major ? 4.0 : 2.5);
    }
    for (int d = 10; d <= 50; d += 10) {
        const QPointF thr = calPt(kCalSignal, -73.f + d);
        const bool major = (d % 20) == 0;
        tick(kArcS, thr, 0, major ? 15 : 8, kRed, major ? 4.0 : 2.5);
    }
    rayLabel(QStringLiteral("1"), {104, 86}, numFont, ink, 0.62);
    rayLabel(QStringLiteral("3"), {200, 60}, numFont, ink, 0.62);
    rayLabel(QStringLiteral("5"), {286, 46}, numFont, ink, 0.62);
    rayLabel(QStringLiteral("7"), {368, 38}, numFont, ink, 0.62);
    rayLabel(QStringLiteral("9"), {450, 35}, numFont, ink, 0.62);
    rayLabel(QStringLiteral("+20"), {564, 41}, numFont, kRed, 0.62);
    rayLabel(QStringLiteral("+40"), {696, 60}, numFont, kRed, 0.62);
    rayLabel(QStringLiteral("+60 dB"), {828, 98}, numFont, kRed, 0.62);
    // End hooks: S0 left, +60 right (red), up from the top line.
    {
        auto endOf = [&](const Arc& a, double t) {
            return QPointF(kPivotX + a.rx * std::cos(qDegreesToRadians(t)),
                           kPivotY - a.ry * std::sin(qDegreesToRadians(t)));
        };
        const QPointF a = endOf(kArcS, kArcS.t0), b = endOf(kArcS, kArcS.t1);
        poly({a + QPointF(-8, -14), a}, ink, 4.5);
        poly({b, b + QPointF(10, -14)}, kRed, 4.5);
    }
    // PO ticks, down from the S base line: long at the labelled marks.
    for (float w : {0.f, 5.f, 10.f, 25.f, 50.f, 100.f, 150.f})
        tick(kArcBase, calPt(kCalPower, w), 0, -14, ink, 4.0);
    for (float w : {30.f, 40.f, 60.f, 70.f, 80.f, 90.f, 110.f, 120.f, 130.f, 140.f})
        tick(kArcBase, calPt(kCalPower, w), 0, -8, ink, 2.5);

    // ── PO / SWR ───────────────────────────────────────────────────────────
    strokeArc(kArcPo, kArcPo.t0, kArcPo.t1, ink, kArcPo.width);
    const double tSwr2 = angleOfValue(kArcSwr, kCalSwr, 2.f);
    const double tSwr3 = angleOfValue(kArcSwr, kCalSwr, 3.f);
    strokeArc(kArcSwr, kArcSwr.t0, tSwr2, kGreen, kArcSwr.width + 1.0, Qt::CustomDashLine);
    strokeArc(kArcSwr, tSwr2, tSwr3, kYellow, kArcSwr.width + 1.0);
    strokeArc(kArcSwr, tSwr3, kArcSwr.t1, kRed, kArcSwr.width + 1.0);
    for (float s : {1.f, 1.5f, 2.f, 3.f})
        tick(kArcPo, calPt(kCalSwr, s), 3, -16, ink, 4.0);
    rayLabel(QStringLiteral("1"), {161, 213}, smallFont, ink, 0.60);
    rayLabel(QStringLiteral("1.5"), {261, 190}, smallFont, ink, 0.60);
    rayLabel(QStringLiteral("2"), {361, 178}, smallFont, ink, 0.60);
    rayLabel(QStringLiteral("3"), {450, 174}, smallFont, ink, 0.60);
    rayLabel(QString(QChar(0x221E)), {745, 206}, smallFont, ink, 0.60);

    // ── Id ─────────────────────────────────────────────────────────────────
    strokeArc(kArcId, kArcId.t0, kArcId.t1, ink, kArcId.width);
    for (int a = 0; a <= 20; a += 2) {
        const bool major = a % 10 == 0;
        tick(kArcId, calPt(kCalAmps, a), 0, major ? -17 : -8, ink, major ? 4.0 : 3.0);
    }
    rayLabel(QStringLiteral("0"), {203, 257}, smallFont, ink, 0.56);
    rayLabel(QStringLiteral("10"), {447, 227}, smallFont, ink, 0.56);
    rayLabel(QStringLiteral("20 A"), {695, 257}, smallFont, ink, 0.56);

    // ── COMP ───────────────────────────────────────────────────────────────
    strokeArc(kArcComp, kArcComp.t0, kArcComp.t1, ink, kArcComp.width);
    for (int c = 0; c <= 30; c += 5) {
        const bool major = c % 10 == 0;
        tick(kArcComp, calPt(kCalComp, c), 0, major ? -14 : -8, ink, major ? 4.5 : 3.0);
    }
    rayLabel(QStringLiteral("0"), {245, 300}, smallFont, ink, 0.48);
    rayLabel(QStringLiteral("10"), {385, 278}, smallFont, ink, 0.48);
    rayLabel(QStringLiteral("20"), {513, 279}, smallFont, ink, 0.48);
    rayLabel(QStringLiteral("30 dB"), {666, 292}, smallFont, ink, 0.48);

    // ── ALC: a red wedge, then a red bracket round the word (as on the image) ─
    {
        QPainterPath wedge;
        wedge.moveTo(map({258, 314.5}));
        wedge.lineTo(map({306, 309.5}));
        wedge.lineTo(map({306, 333.5}));
        wedge.lineTo(map({281, 336}));
        wedge.lineTo(map({258, 318}));
        wedge.closeSubpath();
        p.fillPath(wedge, kRed);
        poly({{300, 311.5}, {350, 307}, {400, 305}, {452, 304}}, kRed, 4.5);
        poly({{452, 304}, {452, 324}, {414, 323.5}}, kRed, 4.5);
        poly({{345, 327.5}, {304, 330.5}}, kRed, 4.5);
        label(QStringLiteral("ALC"), {377, 324}, fontPx(32), kRed);
    }

    // ── Vd: a bracket from 10 to 15 V ──────────────────────────────────────
    poly({{502, 329}, {502, 304}, {600, 311}, {584, 328}}, ink, 4.0);
    label(QStringLiteral("10"), {498, 334}, smallFont, ink);
    label(QStringLiteral("15V"), {586, 340}, smallFont, ink);
    label(QStringLiteral("Vd"), {660, 340}, smallFont, ink);

    // ── Side labels ────────────────────────────────────────────────────────
    label(QStringLiteral("S"), {40, 126}, sideFont, sideInk);
    label(QStringLiteral("PO"), {64, 172}, sideFont, sideInk);
    label(QStringLiteral("SWR"), {96, 218}, sideFont, sideInk);
    label(QStringLiteral("Id"), {126, 258}, sideFont, sideInk);
    label(QStringLiteral("COMP"), {176, 302}, sideFont, sideInk);
}

void AnanMultiMeterWidget::drawPowerLabels(QPainter& p, const QRectF& face) const
{
    // clsNeedleScalePwrItem for the ANAN MM: 7 marks along the PO
    // calibration (0, 5, 10, 25, 50, 100, 150 of 100 W, times MaxPower/100),
    // at LengthFactor 1.51 / RadiusRatio (1, 0.55), each turned along its ray
    // plus Thetis's own correction; the last one with its "W".
    const double x = face.x(), y = face.y(), w = face.width(), h = face.height();
    const QPointF start(x + w / 2.0 + w * 0.004, y + h / 2.0 + h * 0.736);
    const double rrX = 1.0, rrY = 0.55;
    const double radiusX = (w / 2.0) * (1.51 * rrX);
    const double radiusY = (w / 2.0) * (1.51 * rrY);
    struct Mark { float value; double turnDeg; };
    static const Mark marks[] = {{0.f, -11}, {5.f, 8}, {10.f, 10}, {25.f, 5},
                                 {50.f, 0}, {100.f, -8}, {150.f, -10}};
    QFont f(QStringLiteral("Arial"));
    f.setBold(true);
    // FontSize 22: (22 / 16) * (width / 52) DIPs.
    f.setPixelSize(std::max(6, static_cast<int>(std::lround((22.0 / 16.0) * (w / 52.0)))));
    p.setFont(f);
    p.setPen(QColor(128, 128, 128));   // LowColour Gray
    const QFontMetricsF fm(f);
    const bool milliwatts = m_maxPower <= 1.0f;
    for (size_t i = 0; i < std::size(marks); ++i) {
        const QPointF c = interpolate(kCalPower, marks[i].value);
        const double eX = x + c.x() * w, eY = y + c.y() * h;
        const double ang = std::atan2((start.y() - eY) / rrY, (start.x() - eX) / rrX);
        const QPointF end(start.x() + std::cos(ang + M_PI) * radiusX,
                          start.y() + std::sin(ang + M_PI) * radiusY);
        double watts = marks[i].value * (m_maxPower / 100.0);
        if (milliwatts)
            watts *= 1000.0;
        QString text = tidyPower(watts);
        if (i + 1 == std::size(marks))
            text += milliwatts ? QStringLiteral("mW") : QStringLiteral("W");
        const QSizeF sz(fm.horizontalAdvance(text), fm.height());
        p.save();
        p.translate(end);
        p.rotate(qRadiansToDegrees(ang) + marks[i].turnDeg + 270.0);
        p.drawText(QRectF(-sz.width() / 2.0, -sz.height() / 2.0, sz.width() + 1, sz.height()),
                   Qt::AlignCenter, text);
        p.restore();
    }
}

void AnanMultiMeterWidget::drawStrip(QPainter& p, const QRectF& strip)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const double r = strip.height() * 0.18;
    QLinearGradient g(strip.topLeft(), strip.bottomLeft());
    g.setColorAt(0.0, QColor(92, 92, 92));
    g.setColorAt(0.14, QColor(40, 40, 40));
    g.setColorAt(0.85, QColor(22, 22, 22));
    g.setColorAt(1.0, QColor(8, 8, 8));
    QPainterPath path;
    path.addRoundedRect(strip.adjusted(0.5, 0.5, -0.5, -0.5), r, r);
    p.fillPath(path, g);
    p.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
    p.drawPath(path);

    if (m_transmitting) {
        // Thetis's click box (TX only) with the display group's name.
        const QRectF meter(strip.x(), strip.y() - strip.width() * kFaceAspect, strip.width(),
                           strip.width() * (kFaceAspect + kStripAspect));
        const QRectF box = boxRect(meter);
        QLinearGradient bg(box.topLeft(), box.bottomLeft());
        bg.setColorAt(0.0, QColor(20, 20, 20));
        bg.setColorAt(1.0, QColor(58, 58, 58));
        QPainterPath bp;
        bp.addRoundedRect(box, box.height() * 0.15, box.height() * 0.15);
        p.fillPath(bp, bg);
        p.setPen(QPen(QColor(110, 110, 110), 1.0));
        p.drawPath(bp);
        QFont f(QStringLiteral("Arial"));
        f.setBold(true);
        f.setPixelSize(std::max(6, static_cast<int>(box.height() * 0.62)));
        p.setFont(f);
        p.setPen(QColor(169, 169, 169));   // DarkGray
        p.drawText(box, Qt::AlignCenter, displayGroupName(m_displayGroup));
    }
    p.restore();
}

void AnanMultiMeterWidget::mousePressEvent(QMouseEvent* event)
{
    // Thetis: a left click on the box (TX) steps to the next display group.
    if (event->button() == Qt::LeftButton && m_transmitting) {
        const QRectF meter = meterArea(QRectF(rect()));
        if (boxRect(meter).contains(event->position())) {
            setDisplayGroup((m_displayGroup + 1) % kDisplayGroups);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

} // namespace AetherSDR
