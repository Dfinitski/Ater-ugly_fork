#include "gui/WidebandMonitorDialog.h"

#include "core/AppSettings.h"
#include "core/backends/anan/P2Transport.h"
#include "core/backends/hl2/Hl2Backend.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace AetherSDR {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kAdcRateHz = 122.88e6;
constexpr double kNyquistHz = kAdcRateHz / 2.0;
constexpr int kFftSize = 16384;            // one frame: 32 packets x 512
constexpr int kBins = kFftSize / 2 + 1;
constexpr int kAxisH = 18;                 // frequency labels under the trace
constexpr int kLeftW = 46;                 // dB labels

struct HamBand { double lo, hi; };
constexpr HamBand kHamBands[] = {
    {1.8e6, 2.0e6},   {3.5e6, 4.0e6},     {5.06e6, 5.45e6},   {7.0e6, 7.3e6},
    {10.1e6, 10.15e6}, {14.0e6, 14.35e6}, {18.068e6, 18.168e6}, {21.0e6, 21.45e6},
    {24.89e6, 24.99e6}, {28.0e6, 29.7e6}, {50.0e6, 54.0e6}};

QRgb paletteColor(float t)
{
    // Black -> blue -> cyan -> yellow -> red -> white.
    t = std::clamp(t, 0.0f, 1.0f);
    struct Stop { float t; int r, g, b; };
    static constexpr Stop kStops[] = {
        {0.00f, 0, 0, 0},     {0.20f, 0, 0, 140},   {0.40f, 0, 160, 220},
        {0.60f, 230, 230, 0}, {0.80f, 240, 60, 0},  {1.00f, 255, 255, 255}};
    for (int i = 1; i < 6; ++i) {
        if (t <= kStops[i].t) {
            const float u = (t - kStops[i - 1].t) / (kStops[i].t - kStops[i - 1].t);
            const auto mix = [u](int a, int b) { return int(a + (b - a) * u); };
            return qRgb(mix(kStops[i - 1].r, kStops[i].r), mix(kStops[i - 1].g, kStops[i].g),
                        mix(kStops[i - 1].b, kStops[i].b));
        }
    }
    return qRgb(255, 255, 255);
}

QString mhzText(double hz)
{
    return QStringLiteral("%1 MHz").arg(hz / 1.0e6, 0, 'f', 3);
}

} // namespace

// ── View ─────────────────────────────────────────────────────────────────────

WidebandView::WidebandView(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(600, 360);
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

QRect WidebandView::plotRect() const
{
    const int h = height() - kAxisH;
    return QRect(kLeftW, 0, width() - kLeftW, std::max(40, h * 55 / 100));
}

QRect WidebandView::waterfallRect() const
{
    const QRect p = plotRect();
    const int top = p.bottom() + 1 + kAxisH;
    return QRect(kLeftW, top, width() - kLeftW, std::max(1, height() - top));
}

double WidebandView::xToHz(int x) const
{
    const QRect p = plotRect();
    const double t = double(x - p.left()) / std::max(1, p.width() - 1);
    return m_viewLo + t * (m_viewHi - m_viewLo);
}

int WidebandView::hzToX(double hz) const
{
    const QRect p = plotRect();
    return p.left() + int(std::lround((hz - m_viewLo) / (m_viewHi - m_viewLo) * (p.width() - 1)));
}

float WidebandView::levelAt(double hz) const
{
    if (m_db.empty() || m_binHz <= 0.0)
        return m_bottom;
    const int b = std::clamp(int(std::lround(hz / m_binHz)), 0, int(m_db.size()) - 1);
    return m_db[std::size_t(b)];
}

void WidebandView::clampView()
{
    const double minSpan = 200e3;
    double span = std::clamp(m_viewHi - m_viewLo, minSpan, kNyquistHz);
    double lo = std::clamp(m_viewLo, 0.0, kNyquistHz - span);
    m_viewLo = lo;
    m_viewHi = lo + span;
}

void WidebandView::setFrame(const std::vector<float>& db, double binHz)
{
    m_db = db;
    m_binHz = binHz;
    addWaterfallRow();
    update();
}

void WidebandView::setLevels(float topDb, float bottomDb)
{
    m_top = topDb;
    m_bottom = std::min(bottomDb, topDb - 10.0f);
    update();
}

void WidebandView::setMarkerHz(double hz)
{
    if (hz == m_markerHz)
        return;
    m_markerHz = hz;
    update();
}

void WidebandView::clearWaterfall()
{
    const QRect w = waterfallRect();
    m_wf = QImage(std::max(1, w.width()), std::max(1, w.height()), QImage::Format_RGB32);
    m_wf.fill(Qt::black);
    update();
}

void WidebandView::addWaterfallRow()
{
    if (m_wf.isNull() || m_db.empty() || m_binHz <= 0.0)
        return;
    const int w = m_wf.width();
    const int h = m_wf.height();
    if (h > 1) {
        const int bpl = int(m_wf.bytesPerLine());
        uchar* bits = m_wf.bits();
        std::memmove(bits + bpl, bits, std::size_t(bpl) * std::size_t(h - 1));
    }
    auto* row = reinterpret_cast<QRgb*>(m_wf.scanLine(0));
    const float range = std::max(1.0f, m_top - m_bottom);
    for (int x = 0; x < w; ++x) {
        const double hz0 = m_viewLo + (m_viewHi - m_viewLo) * double(x) / w;
        const double hz1 = m_viewLo + (m_viewHi - m_viewLo) * double(x + 1) / w;
        int b0 = int(hz0 / m_binHz), b1 = std::max(b0 + 1, int(hz1 / m_binHz));
        b0 = std::clamp(b0, 0, int(m_db.size()) - 1);
        b1 = std::clamp(b1, b0 + 1, int(m_db.size()));
        float v = -1000.0f;
        for (int b = b0; b < b1; ++b)
            v = std::max(v, m_db[std::size_t(b)]);
        row[x] = paletteColor((v - m_bottom) / range);
    }
}

void WidebandView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    clearWaterfall();
}

void WidebandView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(10, 14, 20));
    const QRect pr = plotRect();
    p.fillRect(pr, QColor(4, 8, 14));

    // Amateur bands, shaded.
    for (const HamBand& b : kHamBands) {
        if (b.hi < m_viewLo || b.lo > m_viewHi)
            continue;
        const int x0 = std::max(pr.left(), hzToX(b.lo));
        const int x1 = std::min(pr.right(), std::max(x0 + 1, hzToX(b.hi)));
        p.fillRect(QRect(x0, pr.top(), x1 - x0 + 1, pr.height()), QColor(30, 60, 40, 140));
    }

    // dB grid.
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    const float range = std::max(1.0f, m_top - m_bottom);
    const int stepDb = range > 80 ? 20 : 10;
    for (int d = int(std::ceil(m_bottom / stepDb)) * stepDb; d <= m_top; d += stepDb) {
        const int y = pr.top() + int((m_top - d) / range * (pr.height() - 1));
        p.setPen(QColor(40, 50, 62));
        p.drawLine(pr.left(), y, pr.right(), y);
        p.setPen(QColor(140, 155, 170));
        p.drawText(QRect(0, y - 7, kLeftW - 4, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(d));
    }

    // Frequency grid.
    const double span = m_viewHi - m_viewLo;
    const double steps[] = {50e3, 100e3, 250e3, 500e3, 1e6, 2e6, 5e6, 10e6};
    double step = 10e6;
    for (double s : steps)
        if (span / s <= 14) { step = s; break; }
    for (double hz = std::ceil(m_viewLo / step) * step; hz <= m_viewHi; hz += step) {
        const int x = hzToX(hz);
        p.setPen(QColor(40, 50, 62));
        p.drawLine(x, pr.top(), x, pr.bottom());
        p.setPen(QColor(140, 155, 170));
        const QString t = step >= 1e6 ? QString::number(hz / 1e6, 'f', 0)
                                      : QString::number(hz / 1e6, 'f', step >= 100e3 ? 1 : 2);
        p.drawText(QRect(x - 30, pr.bottom() + 2, 60, kAxisH - 2), Qt::AlignCenter, t);
    }

    // Trace: peak of the bins under each pixel.
    if (!m_db.empty() && m_binHz > 0.0) {
        QPolygonF poly;
        poly.reserve(pr.width());
        for (int x = pr.left(); x <= pr.right(); ++x) {
            const double hz0 = xToHz(x);
            const double hz1 = xToHz(x + 1);
            int b0 = std::clamp(int(hz0 / m_binHz), 0, int(m_db.size()) - 1);
            int b1 = std::clamp(std::max(b0 + 1, int(hz1 / m_binHz)), b0 + 1, int(m_db.size()));
            float v = -1000.0f;
            for (int b = b0; b < b1; ++b)
                v = std::max(v, m_db[std::size_t(b)]);
            const double y = pr.top() + (m_top - std::clamp(v, m_bottom, m_top)) / range * (pr.height() - 1);
            poly << QPointF(x, y);
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(0, 200, 255), 1.0));
        p.drawPolyline(poly);
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // Receiver marker.
    if (m_markerHz > m_viewLo && m_markerHz < m_viewHi) {
        const int x = hzToX(m_markerHz);
        p.setPen(QPen(QColor(255, 70, 70), 1));
        p.drawLine(x, pr.top(), x, pr.bottom());
        p.drawLine(x, waterfallRect().top(), x, waterfallRect().top() + 6);
    }

    const QRect wr = waterfallRect();
    if (!m_wf.isNull())
        p.drawImage(wr.topLeft(), m_wf);
}

void WidebandView::mouseMoveEvent(QMouseEvent* event)
{
    const int x = int(event->position().x());
    if (m_dragging) {
        const QRect pr = plotRect();
        const double hzPerPx = (m_viewHi - m_viewLo) / std::max(1, pr.width());
        const double span = m_viewHi - m_viewLo;
        m_viewLo = m_dragLo - (x - m_dragX) * hzPerPx;
        m_viewHi = m_viewLo + span;
        clampView();
        update();
        return;
    }
    if (x >= kLeftW) {
        const double hz = xToHz(x);
        emit hoverInfo(QStringLiteral("%1   %2 dBFS").arg(mhzText(hz))
                           .arg(double(levelAt(hz)), 0, 'f', 1));
    }
}

void WidebandView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragX = int(event->position().x());
        m_dragLo = m_viewLo;
    }
}

void WidebandView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (std::abs(int(event->position().x()) - m_dragX) > 2)
            clearWaterfall();
    }
}

void WidebandView::mouseDoubleClickEvent(QMouseEvent* event)
{
    m_dragging = false;
    const int x = int(event->position().x());
    if (x >= kLeftW)
        emit tuneRequested(xToHz(x));
}

void WidebandView::wheelEvent(QWheelEvent* event)
{
    const int x = int(event->position().x());
    const double anchor = xToHz(std::max(x, kLeftW));
    const double factor = event->angleDelta().y() > 0 ? 0.8 : 1.25;
    m_viewLo = anchor - (anchor - m_viewLo) * factor;
    m_viewHi = anchor + (m_viewHi - anchor) * factor;
    clampView();
    clearWaterfall();
}

void WidebandView::leaveEvent(QEvent*)
{
    emit hoverInfo(QString());
}

// ── Dialog ───────────────────────────────────────────────────────────────────

WidebandMonitorDialog::WidebandMonitorDialog(RadioModel* model, QWidget* parent)
    : PersistentDialog(QStringLiteral("Wideband Monitor"), QStringLiteral("WidebandMonitorGeometry"),
                       parent)
    , m_model(model)
{
    auto& st = AppSettings::instance();
    auto* vbox = new QVBoxLayout(bodyWidget());
    vbox->setSpacing(6);

    auto* row = new QHBoxLayout;
    row->setSpacing(6);
    auto addLabel = [row](const QString& t) {
        auto* l = new QLabel(t);
        row->addWidget(l);
    };
    addLabel(QStringLiteral("ADC:"));
    m_adcCombo = new QComboBox;
    m_adcCombo->addItem(QStringLiteral("ADC1 (ANT1-3)"), 0);
    m_adcCombo->addItem(QStringLiteral("ADC2 (RX2 in)"), 1);
    m_adcCombo->setCurrentIndex(std::clamp(st.value(QStringLiteral("AnanWbAdc"), 0).toInt(), 0, 1));
    row->addWidget(m_adcCombo);
    addLabel(QStringLiteral("Update:"));
    m_rateCombo = new QComboBox;
    for (int ms : {20, 50, 100, 200})
        m_rateCombo->addItem(QStringLiteral("%1 ms").arg(ms), ms);
    {
        const int ms = st.value(QStringLiteral("AnanWbRateMs"), 50).toInt();
        const int i = m_rateCombo->findData(ms);
        m_rateCombo->setCurrentIndex(i >= 0 ? i : 1);
    }
    row->addWidget(m_rateCombo);
    addLabel(QStringLiteral("Average:"));
    m_avgSpin = new QSpinBox;
    m_avgSpin->setRange(1, 50);
    m_avgSpin->setValue(st.value(QStringLiteral("AnanWbAverage"), 8).toInt());
    m_avgSpin->setToolTip(QStringLiteral("Frames averaged (power, exponential)"));
    row->addWidget(m_avgSpin);
    addLabel(QStringLiteral("Top:"));
    m_topSpin = new QSpinBox;
    m_topSpin->setRange(-100, 20);
    m_topSpin->setSuffix(QStringLiteral(" dB"));
    m_topSpin->setValue(st.value(QStringLiteral("AnanWbTopDb"), -20).toInt());
    row->addWidget(m_topSpin);
    addLabel(QStringLiteral("Bottom:"));
    m_bottomSpin = new QSpinBox;
    m_bottomSpin->setRange(-180, -20);
    m_bottomSpin->setSuffix(QStringLiteral(" dB"));
    m_bottomSpin->setValue(st.value(QStringLiteral("AnanWbBottomDb"), -130).toInt());
    row->addWidget(m_bottomSpin);
    row->addStretch(1);
    m_info = new QLabel;
    m_info->setMinimumWidth(190);
    m_info->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(m_info);
    vbox->addLayout(row);

    m_view = new WidebandView;
    m_view->setLevels(float(m_topSpin->value()), float(m_bottomSpin->value()));
    vbox->addWidget(m_view, 1);

    m_status = new QLabel(QStringLiteral(
        "Raw ADC spectrum, 0 - 61.44 MHz, 7.5 kHz bins (dBFS per bin). Wheel: zoom, "
        "drag: pan, double-click: tune the active receiver."));
    m_status->setWordWrap(true);
    vbox->addWidget(m_status);

    // FFT tables.
    m_window.resize(kFftSize);
    double sum = 0.0;
    for (int n = 0; n < kFftSize; ++n) {
        const double a = 2.0 * kPi * n / (kFftSize - 1);
        // 4-term Blackman-Harris (-92 dB side lobes).
        const double w = 0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2 * a)
                         - 0.01168 * std::cos(3 * a);
        m_window[std::size_t(n)] = float(w);
        sum += w;
    }
    m_windowSum = float(sum);
    m_bitrev.resize(kFftSize);
    int bits = 0;
    while ((1 << bits) < kFftSize)
        ++bits;
    for (int i = 0; i < kFftSize; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b))
                r |= 1 << (bits - 1 - b);
        m_bitrev[std::size_t(i)] = r;
    }
    m_re.resize(kFftSize);
    m_im.resize(kFftSize);

    connect(m_view, &WidebandView::hoverInfo, m_info, &QLabel::setText);
    connect(m_view, &WidebandView::tuneRequested, this, [this](double hz) {
        if (!m_model)
            return;
        for (SliceModel* s : m_model->slices()) {
            if (s && s->isActive()) {
                s->tuneAndRecenter(hz / 1.0e6);
                break;
            }
        }
    });
    auto levelsChanged = [this] {
        m_view->setLevels(float(m_topSpin->value()), float(m_bottomSpin->value()));
        saveSettings();
    };
    connect(m_topSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, levelsChanged);
    connect(m_bottomSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, levelsChanged);
    connect(m_avgSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { saveSettings(); });
    auto streamChanged = [this] {
        saveSettings();
        m_avgPower.clear();
        m_view->clearWaterfall();
        if (m_enabled)
            pushEnable(true);
    };
    connect(m_adcCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, streamChanged);
    connect(m_rateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, streamChanged);

    // A new backend on every connect: follow it.
    if (m_model) {
        connect(m_model, &RadioModel::capabilitiesChanged, this, [this](bool, const RadioCapabilities&) {
            attachTransport();
            if (m_enabled)
                pushEnable(true);
        });
    }
    // Receiver marker.
    auto* markerTimer = new QTimer(this);
    markerTimer->setInterval(250);
    connect(markerTimer, &QTimer::timeout, this, [this] {
        if (!m_model)
            return;
        for (SliceModel* s : m_model->slices()) {
            if (s && s->isActive()) {
                m_view->setMarkerHz(s->frequency() * 1.0e6);
                return;
            }
        }
    });
    markerTimer->start();
    resize(1000, 620);
    attachTransport();
}

WidebandMonitorDialog::~WidebandMonitorDialog()
{
    pushEnable(false);
}

void WidebandMonitorDialog::saveSettings()
{
    auto& st = AppSettings::instance();
    st.setValue(QStringLiteral("AnanWbAdc"), m_adcCombo->currentData().toInt());
    st.setValue(QStringLiteral("AnanWbRateMs"), m_rateCombo->currentData().toInt());
    st.setValue(QStringLiteral("AnanWbAverage"), m_avgSpin->value());
    st.setValue(QStringLiteral("AnanWbTopDb"), m_topSpin->value());
    st.setValue(QStringLiteral("AnanWbBottomDb"), m_bottomSpin->value());
    st.save();
}

void WidebandMonitorDialog::attachTransport()
{
    QObject* t = nullptr;
    if (m_model) {
        if (auto* be = dynamic_cast<hl2::Hl2Backend*>(m_model->backend()))
            t = be->ananTransport();
    }
    if (t == m_transport.data() && m_frameConn)
        return;
    QObject::disconnect(m_frameConn);
    m_frameConn = {};
    m_transport = t;
    if (!t) {
        m_status->setText(QStringLiteral("No radio connected."));
        qInfo() << "Wideband Monitor: no ANAN P2 transport (radio not connected?)";
        return;
    }
    qInfo() << "Wideband Monitor: attached to the ANAN P2 transport";
    m_frameConn = connect(static_cast<anan::P2Transport*>(t), &anan::P2Transport::widebandFrame,
                          this, &WidebandMonitorDialog::onFrame, Qt::QueuedConnection);
}

void WidebandMonitorDialog::pushEnable(bool on)
{
    m_enabled = on;
    if (!m_transport)
        return;
    const int mask = on ? (1 << m_adcCombo->currentData().toInt()) : 0;
    QMetaObject::invokeMethod(m_transport.data(), "setWideband", Qt::QueuedConnection,
                              Q_ARG(int, mask), Q_ARG(int, m_rateCombo->currentData().toInt()));
}

void WidebandMonitorDialog::showEvent(QShowEvent* event)
{
    PersistentDialog::showEvent(event);
    attachTransport();
    pushEnable(true);
}

void WidebandMonitorDialog::hideEvent(QHideEvent* event)
{
    pushEnable(false);
    PersistentDialog::hideEvent(event);
}

void WidebandMonitorDialog::onFrame(int adc, const QVector<float>& samples)
{
    if (m_framesLogged < 1) {
        ++m_framesLogged;
        qInfo() << "Wideband Monitor: first frame, ADC" << adc + 1 << "," << samples.size() << "samples";
    }
    if (!m_enabled || adc != m_adcCombo->currentData().toInt()
        || samples.size() < kFftSize)
        return;
    // Windowed, mean removed (DC offset of the converter).
    double mean = 0.0;
    for (int n = 0; n < kFftSize; ++n)
        mean += samples[n];
    mean /= kFftSize;
    for (int n = 0; n < kFftSize; ++n) {
        const int r = m_bitrev[std::size_t(n)];
        m_re[std::size_t(r)] = float((samples[n] - mean) * m_window[std::size_t(n)]);
        m_im[std::size_t(r)] = 0.0f;
    }
    // Iterative radix-2 FFT.
    for (int len = 2; len <= kFftSize; len <<= 1) {
        const double ang = -2.0 * kPi / len;
        const float wr = float(std::cos(ang)), wi = float(std::sin(ang));
        const int half = len >> 1;
        for (int i = 0; i < kFftSize; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int j = 0; j < half; ++j) {
                const std::size_t a = std::size_t(i + j), b = std::size_t(i + j + half);
                const float tr = m_re[b] * cr - m_im[b] * ci;
                const float ti = m_re[b] * ci + m_im[b] * cr;
                m_re[b] = m_re[a] - tr;
                m_im[b] = m_im[a] - ti;
                m_re[a] += tr;
                m_im[a] += ti;
                const float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
    // Power per bin, full-scale sine = 0 dBFS: |X| * 2 / sum(w).
    const float norm = 2.0f / m_windowSum;
    if (m_avgPower.size() != std::size_t(kBins))
        m_avgPower.assign(std::size_t(kBins), -1.0f);
    const float alpha = 1.0f / float(std::max(1, m_avgSpin->value()));
    m_db.resize(std::size_t(kBins));
    for (int k = 0; k < kBins; ++k) {
        const float mr = m_re[std::size_t(k)] * norm, mi = m_im[std::size_t(k)] * norm;
        const float pw = mr * mr + mi * mi;
        float& avg = m_avgPower[std::size_t(k)];
        avg = avg < 0.0f ? pw : avg + (pw - avg) * alpha;
        m_db[std::size_t(k)] = 10.0f * std::log10(std::max(avg, 1e-20f));
    }
    m_view->setFrame(m_db, kNyquistHz / (kBins - 1));
    if (++m_frames == 1)
        m_status->setText(QStringLiteral(
            "Raw ADC spectrum, 0 - 61.44 MHz, 7.5 kHz bins (dBFS per bin). Wheel: zoom, "
            "drag: pan, double-click: tune the active receiver."));
}

} // namespace AetherSDR
