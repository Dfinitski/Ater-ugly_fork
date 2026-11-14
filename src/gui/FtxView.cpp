// The FTX view: see FtxView.h. The waterfall is MicronSDR's FtWaterfall.cpp.
#include "gui/FtxView.h"

#include "core/ftx/FtxState.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace AetherSDR {

// ---------------------------------------------------------------------------
// FtxWaterfall

FtxWaterfall::FtxWaterfall(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(200, 60);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::CrossCursor);
    // The colours of the waterfall of JTDX: dark blue noise, blue, cyan, the
    // signals up to bright yellow.
    static const struct { double t; int r, g, b; } stop[] = {
        {0.00, 0, 0, 24},     {0.20, 0, 12, 100},    {0.40, 0, 64, 210},  {0.56, 0, 165, 255},
        {0.70, 0, 240, 240},  {0.84, 170, 255, 110}, {1.00, 255, 255, 0}};
    const int ns = static_cast<int>(sizeof(stop) / sizeof(stop[0]));
    for (int i = 0; i < 256; ++i) {
        const double t = i / 255.0;
        int k = 0;
        while (k < ns - 2 && t > stop[k + 1].t)
            ++k;
        const double f = (t - stop[k].t) / (stop[k + 1].t - stop[k].t);
        m_pal[i] = qRgb(static_cast<int>(stop[k].r + f * (stop[k + 1].r - stop[k].r)),
                        static_cast<int>(stop[k].g + f * (stop[k + 1].g - stop[k].g)),
                        static_cast<int>(stop[k].b + f * (stop[k + 1].b - stop[k].b)));
    }
    setSubMode(ftx::FtxState::instance().subMode);
}

void FtxWaterfall::setRange(int fromHz, int toHz)
{
    fromHz = std::clamp(fromHz, 0, 5500);
    toHz = std::clamp(toHz, fromHz + 200, 6000);
    if (fromHz == m_f0 && toHz == m_f1)
        return;
    m_f0 = fromHz;
    m_f1 = toHz;
    clear();   // the old lines are of another range
}

void FtxWaterfall::setSubMode(int mode)
{
    m_mode = mode;
    m_bw = ftx::signalWidthHz(mode);
    m_lastPeriod = -1;
    update();
}

void FtxWaterfall::clear()
{
    if (!m_img.isNull())
        m_img.fill(m_pal[0]);
    m_lastPeriod = -1;
    update();
}

double FtxWaterfall::xToHz(int x) const
{
    return m_f0 + static_cast<double>(x) * (m_f1 - m_f0) / std::max(1, width());
}

int FtxWaterfall::hzToX(double hz) const
{
    return static_cast<int>(std::lround((hz - m_f0) * width() / static_cast<double>(m_f1 - m_f0)));
}

void FtxWaterfall::resizeEvent(QResizeEvent*)
{
    const int w = std::max(1, width()), h = std::max(1, height() - kScalePx);
    if (m_img.isNull()) {
        m_img = QImage(w, h, QImage::Format_RGB32);
        m_img.fill(m_pal[0]);
    } else if (m_img.width() != w || m_img.height() != h) {
        // The history kept: the same lines stretched to the new width, cut or
        // filled at the bottom.
        QImage n(w, h, QImage::Format_RGB32);
        n.fill(m_pal[0]);
        QPainter p(&n);
        p.drawImage(0, 0, m_img.scaled(w, m_img.height()));
        p.end();
        m_img = n;
    }
}

void FtxWaterfall::addLine(const QVector<float>& db, qint64 tMs)
{
    if (m_img.isNull() || db.size() < kBins)
        return;
    const int w = m_img.width(), h = m_img.height();
    const double hzBin = binHz();
    // The levels by themselves: the noise floor (25 % of the bins shown) and
    // the span of the signals.
    const int k0 = std::clamp(static_cast<int>(m_f0 / hzBin), 0, kBins - 1);
    const int k1 = std::clamp(static_cast<int>(std::ceil(m_f1 / hzBin)), k0 + 1, kBins);
    std::vector<float> v(db.constBegin() + k0, db.constBegin() + k1);
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 4), v.end());
    const float fl = v[v.size() / 4];
    const std::size_t topAt = (v.size() * 995) / 1000;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(topAt), v.end());
    const float top = v[topAt];
    const float span = std::clamp(top - fl + 4.0f, 20.0f, 50.0f);
    if (!m_levelInit) {
        m_floorDb = fl;
        m_spanDb = span;
        m_levelInit = true;
    } else {
        m_floorDb += 0.15f * (fl - m_floorDb);                              // about 2 s
        m_spanDb += (span > m_spanDb ? 0.05f : 0.01f) * (span - m_spanDb);  // a strong signal: up faster
    }
    // The image one line down, the new line at the top.
    uchar* bits = m_img.bits();
    const int bpl = static_cast<int>(m_img.bytesPerLine());
    if (h > 1)
        std::memmove(bits + bpl, bits, static_cast<std::size_t>(bpl) * static_cast<std::size_t>(h - 1));
    QRgb* row = reinterpret_cast<QRgb*>(bits);
    const float lo = m_floorDb - 3.0f, k = 1.0f / (m_spanDb + 3.0f);   // the noise: dark blue
    for (int x = 0; x < w; ++x) {
        const double ha = xToHz(x) / hzBin, hb = xToHz(x + 1) / hzBin;
        float val;
        if (hb - ha >= 1.0) {
            // Several bins in a pixel: the strongest.
            const int a = std::clamp(static_cast<int>(std::floor(ha)), 0, kBins - 1);
            const int b = std::clamp(static_cast<int>(std::ceil(hb)) - 1, a, kBins - 1);
            val = db[a];
            for (int i = a + 1; i <= b; ++i)
                val = std::max(val, db[i]);
        } else {
            // Less than a bin: between the two nearest.
            const double c = (ha + hb) / 2.0 - 0.5;
            const int a = std::clamp(static_cast<int>(std::floor(c)), 0, kBins - 2);
            const float f = static_cast<float>(std::clamp(c - a, 0.0, 1.0));
            val = db[a] + f * (db[a + 1] - db[a]);
        }
        const int i = std::clamp(static_cast<int>((val - lo) * k * 255.0f), 0, 255);
        row[x] = m_pal[i];
    }
    // The start of a period: a line and its UTC.
    const int period = ftx::periodMs(m_mode);
    const qint64 per = tMs / period;
    if (m_lastPeriod >= 0 && per != m_lastPeriod) {
        for (int x = 0; x < w; x += 2)
            row[x] = qRgb(150, 150, 150);
        QPainter p(&m_img);
        QFont f = font();
        f.setPointSize(7);
        p.setFont(f);
        p.setPen(QColor(220, 220, 220));
        p.drawText(3, 10, QDateTime::fromMSecsSinceEpoch(per * period, QTimeZone::UTC)
                              .toString(m_mode == ftx::kWspr ? QStringLiteral("HH:mm")
                                                             : QStringLiteral("HH:mm:ss")));
    }
    m_lastPeriod = per;
    update();
}

void FtxWaterfall::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const int w = width();
    // The scale.
    p.fillRect(0, 0, w, kScalePx, QColor(28, 28, 28));
    const double pxHz = w / static_cast<double>(m_f1 - m_f0);
    int step = 100;
    for (int s : {100, 200, 250, 500, 1000}) {
        step = s;
        if (s * pxHz >= 55)
            break;
    }
    const int minor = (step == 250) ? 50 : step / 5 >= 20 ? step / 5 : step / 2;
    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);
    for (int hz = (m_f0 / minor) * minor; hz <= m_f1; hz += minor) {
        if (hz < m_f0)
            continue;
        const int x = hzToX(hz);
        const bool major = (hz % step) == 0;
        p.setPen(QColor(150, 150, 150));
        p.drawLine(x, kScalePx - 1, x, kScalePx - (major ? 9 : 5));
        if (major) {
            p.setPen(QColor(220, 220, 220));
            const QString t = QString::number(hz);
            const int tw = p.fontMetrics().horizontalAdvance(t);
            p.drawText(std::clamp(x - tw / 2, 1, std::max(1, w - tw - 1)), 11, t);
        }
    }
    // The band of the TX signal (red, above) and of the RX one (green).
    auto band = [&](int hz, int y, const QColor& c) {
        const int xa = hzToX(hz), xb = std::max(xa + 2, hzToX(hz + m_bw));
        p.setPen(QPen(c, 2));
        p.drawLine(xa, y, xb, y);
        p.drawLine(xa, y - 3, xa, y + 1);
        p.drawLine(xb, y - 3, xb, y + 1);
    };
    band(m_tx, 15, QColor(255, 60, 60));
    band(m_rx, 20, QColor(60, 230, 60));
    // The waterfall.
    if (!m_img.isNull())
        p.drawImage(0, kScalePx, m_img);
    // The RX and TX bands: two lines each, the start and the end of the signal.
    p.setPen(QPen(QColor(255, 60, 60), 1));
    p.drawLine(hzToX(m_tx), kScalePx, hzToX(m_tx), height());
    p.drawLine(hzToX(m_tx + m_bw), kScalePx, hzToX(m_tx + m_bw), height());
    p.setPen(QPen(QColor(60, 230, 60), 1));
    p.drawLine(hzToX(m_rx), kScalePx, hzToX(m_rx), height());
    p.drawLine(hzToX(m_rx + m_bw), kScalePx, hzToX(m_rx + m_bw), height());
    // The frequency under the mouse.
    if (m_hoverX >= 0) {
        const QString t = QStringLiteral("%1 Hz").arg(qRound(xToHz(m_hoverX)));
        const int tw = p.fontMetrics().horizontalAdvance(t) + 8;
        int x = m_hoverX + 6;
        if (x + tw > w)
            x = m_hoverX - 6 - tw;
        p.fillRect(x, kScalePx + 2, tw, 15, QColor(0, 0, 0, 170));
        p.setPen(Qt::white);
        p.drawText(QRect(x, kScalePx + 2, tw, 15), Qt::AlignCenter, t);
        p.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DotLine));
        p.drawLine(m_hoverX, kScalePx, m_hoverX, height());
    }
}

// The RX (left button) / TX (right button) band centred on x.
void FtxWaterfall::setFreqAt(int x)
{
    const int hz = std::clamp(static_cast<int>(std::lround(xToHz(x) - m_bw / 2.0)), m_f0,
                              static_cast<int>(m_f1 - m_bw));
    if (m_freqDrag == Qt::LeftButton && hz != m_rx) {
        m_rx = hz;
        emit rxClicked(hz);
    } else if (m_freqDrag == Qt::RightButton && hz != m_tx) {
        m_tx = hz;
        emit txClicked(hz);
    }
    update();
}

void FtxWaterfall::mousePressEvent(QMouseEvent* e)
{
    if (e->position().y() < kScalePx)
        return;
    if (e->button() == Qt::LeftButton || e->button() == Qt::RightButton) {
        m_freqDrag = e->button();   // held: the band follows the mouse
        setFreqAt(static_cast<int>(e->position().x()));
    }
}

void FtxWaterfall::mouseMoveEvent(QMouseEvent* e)
{
    m_hoverX = e->position().y() >= kScalePx ? static_cast<int>(e->position().x()) : -1;
    if (m_freqDrag != Qt::NoButton)
        setFreqAt(static_cast<int>(e->position().x()));
    update();
}

void FtxWaterfall::mouseReleaseEvent(QMouseEvent*)
{
    m_freqDrag = Qt::NoButton;
}

void FtxWaterfall::leaveEvent(QEvent*)
{
    m_hoverX = -1;
    update();
}

// ---------------------------------------------------------------------------
// FtxView

namespace {

// In-place radix-2 FFT (n a power of two). 2048 points four times in 0.34 s:
// no library is worth bringing in for it.
void fftInPlace(std::vector<std::complex<float>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<float> wl(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

QPushButton* barButton(const QString& text)
{
    auto* b = new QPushButton(text);
    b->setCheckable(true);
    b->setFixedHeight(22);
    b->setMinimumWidth(52);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

}  // namespace

FtxView::FtxView(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(16, 18, 22));
    setPalette(pal);

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->setSpacing(4);
    auto* title = new QLabel(QStringLiteral("FTX"));
    title->setStyleSheet(QStringLiteral("color:#e0e0e0; font-weight:bold;"));
    bar->addWidget(title);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    const int current = ftx::FtxState::instance().subMode;
    for (int m : {ftx::kFt8, ftx::kFt4, ftx::kJs8, ftx::kWspr}) {
        QPushButton* b = barButton(QString::fromLatin1(ftx::subModeName(m)));
        b->setChecked(m == current);
        group->addButton(b, m);
        bar->addWidget(b);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int m) {
        ftx::FtxState::instance().setSubMode(m);
        m_waterfall->setSubMode(m);
        m_waterfall->clear();
        emit subModeChanged(m);
    });
    m_dial = new QLabel;
    m_dial->setStyleSheet(QStringLiteral("color:#c8c8c8;"));
    bar->addSpacing(12);
    bar->addWidget(m_dial);
    bar->addStretch(1);
    auto* exitBtn = new QPushButton(QStringLiteral("Exit FTX"));
    exitBtn->setFixedHeight(22);
    exitBtn->setFocusPolicy(Qt::NoFocus);
    exitBtn->setToolTip(QStringLiteral("Leave the FTX mode: the receiver, its panadapter and its sound as before."));
    connect(exitBtn, &QPushButton::clicked, this, &FtxView::exitRequested);
    bar->addWidget(exitBtn);
    vbox->addLayout(bar);

    // The waterfall: a low strip, as in MicronSDR.
    m_waterfall = new FtxWaterfall(this);
    m_waterfall->setFixedHeight(170);
    vbox->addWidget(m_waterfall);

    // The room for the decode windows (the next stages of the port).
    auto* note = new QLabel(QStringLiteral(
        "FTX: the receiver is taken over by the mode (no sound to the loudspeakers, no microphone to the "
        "transmitter).\nDecoders, the message windows and the transmitter are the next stages of the port."));
    note->setAlignment(Qt::AlignCenter);
    note->setStyleSheet(QStringLiteral("color:#707880;"));
    vbox->addWidget(note, 1);

    // 24 -> 12 kHz: windowed-sinc low-pass at a quarter of the input rate
    // (6 kHz), Blackman, 63 taps: the image of 6 .. 12 kHz is down > 70 dB
    // from about 6.8 kHz; the decoders' band ends at 4 kHz.
    constexpr int kTaps = 63;
    m_decTaps.resize(kTaps);
    double sum = 0.0;
    for (int i = 0; i < kTaps; ++i) {
        const int k = i - kTaps / 2;
        const double x = 0.5 * k;
        const double sinc = k == 0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
        const double w = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kTaps - 1))
                       + 0.08 * std::cos(4.0 * std::numbers::pi * i / (kTaps - 1));
        m_decTaps[static_cast<std::size_t>(i)] = static_cast<float>(sinc * w);
        sum += sinc * w;
    }
    for (float& t : m_decTaps)
        t = static_cast<float>(t / sum);
    m_decHist.assign(kTaps, 0.0f);

    m_buf.assign(FtxWaterfall::kFft, 0.0f);
    m_window.resize(FtxWaterfall::kFft);
    for (int i = 0; i < FtxWaterfall::kFft; ++i)
        m_window[static_cast<std::size_t>(i)] =
            static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / FtxWaterfall::kFft));
    m_power.assign(FtxWaterfall::kBins, 0.0);
    m_work.resize(FtxWaterfall::kFft);
}

void FtxView::setDialText(const QString& text)
{
    m_dial->setText(text);
}

void FtxView::feedAudio(const QVector<float>& stereo24k)
{
    const qsizetype frames = stereo24k.size() / 2;
    if (frames <= 0)
        return;
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(frames / 2 + 1));
    const std::size_t taps = m_decTaps.size();
    for (qsizetype i = 0; i < frames; ++i) {
        m_decHist[m_decPos] = 0.5f * (stereo24k[2 * i] + stereo24k[2 * i + 1]);
        m_decPhase = !m_decPhase;
        if (m_decPhase) {   // every second input sample: one output sample
            float acc = 0.0f;
            std::size_t idx = m_decPos;
            for (std::size_t t = 0; t < taps; ++t) {
                acc += m_decTaps[t] * m_decHist[idx];
                idx = idx == 0 ? taps - 1 : idx - 1;
            }
            out.push_back(acc);
        }
        m_decPos = (m_decPos + 1) % taps;
    }
    pushAudio12k(out.data(), out.size());
}

void FtxView::pushAudio12k(const float* a, std::size_t n)
{
    constexpr std::size_t kN = FtxWaterfall::kFft, kHop = kN / 2;
    for (std::size_t i = 0; i < n; ++i) {
        m_buf[m_bufFill++] = a[i];
        if (m_bufFill == kN) {
            fftLine();
            // Half overlapped: the second half becomes the first.
            std::copy(m_buf.begin() + static_cast<std::ptrdiff_t>(kHop), m_buf.end(), m_buf.begin());
            m_bufFill = kHop;
        }
    }
}

void FtxView::fftLine()
{
    constexpr int kAvg = 4;
    for (std::size_t i = 0; i < m_work.size(); ++i)
        m_work[i] = std::complex<float>(m_buf[i] * m_window[i], 0.0f);
    fftInPlace(m_work);
    for (std::size_t k = 0; k < m_power.size(); ++k)
        m_power[k] += static_cast<double>(std::norm(m_work[k]));
    if (++m_ffts < kAvg)
        return;
    QVector<float> db(FtxWaterfall::kBins);
    for (int k = 0; k < FtxWaterfall::kBins; ++k) {
        db[k] = static_cast<float>(10.0 * std::log10(m_power[static_cast<std::size_t>(k)] / kAvg + 1e-20));
        m_power[static_cast<std::size_t>(k)] = 0.0;
    }
    m_ffts = 0;
    m_waterfall->addLine(db, QDateTime::currentMSecsSinceEpoch());
}

}  // namespace AetherSDR
