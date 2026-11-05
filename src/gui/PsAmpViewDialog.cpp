#include "gui/PsAmpViewDialog.h"

#include "gui/PsApplet.h"
#include "core/backends/anan/AnanPureSignal.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr double kPi = 3.14159265358979323846;
const char* kStateNames[] = {"RESET", "WAIT", "MOXDELAY", "SETUP", "COLLECT",
                             "MOXCHECK", "CALC", "DELAY", "STAYON", "TURNON"};

QString psDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                      + QStringLiteral("/AetherSDR/PureSignal");
    QDir().mkpath(d);
    return d;
}

} // namespace

// ── Curves ───────────────────────────────────────────────────────────────────

PsCurveView::PsCurveView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(460, 360);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PsCurveView::setCurves(const Curves& c)
{
    m_c = c;
    // Phase axis span. The amplifier phase of the smallest samples (input
    // < 0.15, output near zero in the class-AB dead zone) is noise up to
    // +-180 deg; letting it set the scale made the whole yellow curve jump
    // between spans from one calibration to the next. Those points are still
    // drawn, the scale follows the rest, and it shrinks only after the
    // content has stayed under 60 % of it for 5 calibrations.
    double mx = 0.0;
    for (const auto& p : m_c.amPm)
        if (p.x() >= 0.15)
            mx = std::max(mx, std::abs(p.y()));
    for (const auto& p : m_c.corrPhs)
        if (p.x() >= 0.15)
            mx = std::max(mx, std::abs(p.y()));
    const double want = std::clamp(std::ceil(mx / 10.0) * 10.0, 10.0, 180.0);
    if (want > m_phsSpan) {
        m_phsSpan = want;
        m_phsShrink = 0;
    } else if (mx < 0.6 * m_phsSpan) {
        if (++m_phsShrink >= 5) {
            m_phsSpan = want;
            m_phsShrink = 0;
        }
    } else {
        m_phsShrink = 0;
    }
    update();
}

void PsCurveView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(10, 14, 20));
    const QRect r = rect().adjusted(46, 12, -46, -30);
    p.fillRect(r, QColor(4, 8, 14));
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);

    auto X = [&](double v) { return r.left() + v * r.width(); };
    auto Ym = [&](double v) { return r.bottom() - v / 1.2 * r.height(); };          // 0..1.2
    auto Yp = [&](double deg) { return r.center().y() - deg / m_phsSpan * (r.height() / 2.0); };

    // Grid and axes.
    for (int i = 0; i <= 10; ++i) {
        const double x = X(i / 10.0);
        p.setPen(QColor(36, 46, 58));
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.setPen(QColor(150, 160, 175));
        p.drawText(QRectF(x - 16, r.bottom() + 2, 32, 14), Qt::AlignCenter,
                   QString::number(i / 10.0, 'f', 1));
    }
    for (int i = 0; i <= 6; ++i) {
        const double v = i * 0.2;
        const double y = Ym(v);
        p.setPen(QColor(36, 46, 58));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(QColor(120, 200, 255));
        p.drawText(QRectF(0, y - 7, 42, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(v, 'f', 1));
    }
    for (int i = -2; i <= 2; ++i) {
        const double deg = m_phsSpan * i / 2.0;
        p.setPen(QColor(255, 200, 80));
        p.drawText(QRectF(r.right() + 4, Yp(deg) - 7, 42, 14), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1°").arg(deg, 0, 'f', 0));
    }
    p.setPen(QColor(150, 160, 175));
    p.drawText(QRectF(r.left(), r.bottom() + 15, r.width(), 14), Qt::AlignCenter,
               tr("Input magnitude"));

    // Ideal: a linear amplifier (diagonal) and no phase change.
    p.setPen(QPen(QColor(110, 110, 110), 1, Qt::DashLine));
    p.drawLine(QPointF(X(0), Ym(0)), QPointF(X(1), Ym(1)));
    p.drawLine(QPointF(X(0), Yp(0)), QPointF(X(1), Yp(0)));

    p.setRenderHint(QPainter::Antialiasing, true);
    // Amplifier: dots.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 190, 255));
    for (const auto& pt : m_c.amAm)
        if (pt.x() >= 0 && pt.x() <= 1.05)
            p.drawEllipse(QPointF(X(pt.x()), Ym(pt.y())), 1.3, 1.3);
    p.setBrush(QColor(255, 200, 80));
    for (const auto& pt : m_c.amPm)
        if (pt.x() >= 0 && pt.x() <= 1.05)
            p.drawEllipse(QPointF(X(pt.x()), Yp(pt.y())), 1.3, 1.3);
    p.setBrush(Qt::NoBrush);
    // Correction: lines.
    auto line = [&](const std::vector<QPointF>& pts, bool phase, const QColor& c) {
        if (pts.size() < 2)
            return;
        QPainterPath path;
        bool first = true;
        for (const auto& pt : pts) {
            const QPointF q(X(pt.x()), phase ? Yp(pt.y()) : Ym(pt.y()));
            if (first) {
                path.moveTo(q);
                first = false;
            } else {
                path.lineTo(q);
            }
        }
        p.setPen(QPen(c, 2));
        p.drawPath(path);
    };
    line(m_c.corrMag, false, QColor(80, 230, 120));
    line(m_c.corrPhs, true, QColor(240, 90, 200));
    p.setRenderHint(QPainter::Antialiasing, false);

    // Legend.
    struct L { QColor c; QString t; };
    const L legend[] = {{QColor(0, 190, 255), tr("Amplifier AM/AM")},
                        {QColor(255, 200, 80), tr("Amplifier AM/PM")},
                        {QColor(80, 230, 120), tr("Correction gain")},
                        {QColor(240, 90, 200), tr("Correction phase")}};
    int ly = r.top() + 6;
    for (const L& l : legend) {
        p.fillRect(QRect(r.left() + 8, ly + 3, 12, 6), l.c);
        p.setPen(QColor(200, 210, 220));
        p.drawText(QRect(r.left() + 24, ly - 2, 160, 14), Qt::AlignLeft | Qt::AlignVCenter, l.t);
        ly += 14;
    }
    if (m_c.amAm.empty() && m_c.corrMag.empty()) {
        p.setPen(QColor(150, 160, 175));
        p.drawText(r, Qt::AlignCenter,
                   tr("No calibration yet.\nPS on, then transmit (2 Tone) to calibrate."));
    }
}

// ── Dialog ───────────────────────────────────────────────────────────────────

PsAmpViewDialog::PsAmpViewDialog(PsApplet* applet, QWidget* parent)
    : PersistentDialog(tr("PureSignal - Amp View"), QStringLiteral("PsAmpViewGeometry"), parent)
    , m_applet(applet)
{
    auto* top = new QHBoxLayout(bodyWidget());
    top->setSpacing(8);
    auto* left = new QVBoxLayout;
    left->setSpacing(6);
    m_view = new PsCurveView;
    left->addWidget(m_view, 3);
    m_spec = new PsSpectrumView;
    left->addWidget(m_spec, 2);
    top->addLayout(left, 1);

    // The side panel has a fixed width, so text changes (status, IMD) never
    // resize the window (operator 01.11: the window kept jumping).
    auto* sidePanel = new QWidget;
    sidePanel->setFixedWidth(380);
    auto* side = new QVBoxLayout(sidePanel);
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(6);
    top->addWidget(sidePanel);

    // Tests.
    auto* tests = new QGroupBox(tr("Tests"));
    auto* tg = new QGridLayout(tests);
    m_twoTone = new QPushButton(tr("2 Tone"));
    m_twoTone->setCheckable(true);
    m_twoTone->setToolTip(tr("Transmit the two-tone test signal (SSB only)"));
    m_cal = new QPushButton(tr("Calibrate"));
    m_cal->setCheckable(true);
    m_cal->setToolTip(tr("One calibration (Single); lit until it is applied"));
    m_auto = new QPushButton(tr("Auto"));
    m_auto->setCheckable(true);
    m_auto->setToolTip(tr("Calibrate continuously while transmitting"));
    m_single = new QPushButton(tr("Single"));
    m_single->setCheckable(true);
    m_single->setToolTip(tr("Keep the correction of one calibration"));
    m_reset = new QPushButton(tr("Reset"));
    m_reset->setToolTip(tr("Drop the correction and start again"));
    // Latched buttons show their state (operator: "they do not stay pressed").
    const QString latch = QStringLiteral(
        "QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #40b0ff; }");
    for (QPushButton* b : {m_twoTone, m_cal, m_auto, m_single})
        b->setStyleSheet(latch);
    m_twoTone->setStyleSheet(QStringLiteral(
        "QPushButton:checked { background: #c03030; color: #ffffff; border: 1px solid #ff5050; }"));
    tg->addWidget(m_twoTone, 0, 0, 1, 2);
    tg->addWidget(m_auto, 1, 0);
    tg->addWidget(m_single, 1, 1);
    tg->addWidget(m_cal, 2, 0);
    tg->addWidget(m_reset, 2, 1);
    m_hold = new QCheckBox(tr("Hold curves"));
    tg->addWidget(m_hold, 4, 0, 1, 2);
    m_measDelay = new QPushButton(tr("Measure delay"));
    m_measDelay->setToolTip(tr("Once per band: transmit 2 Tone (or voice) at LOW power\n"
                               "(linear region, e.g. 5-10 W), press, wait ~6 s. The correction is\n"
                               "switched off while measuring; the delay is then fixed and saved for the band.\n"
                               "Used unchanged at any power. Refused when the amplifier compresses\n"
                               "by more than 1.5 dB at the peak."));
    tg->addWidget(m_measDelay, 3, 0, 1, 2);
    connect(m_measDelay, &QPushButton::clicked, this, [this] {
        if (auto* ps = m_applet ? m_applet->pureSignal() : nullptr)
            ps->startDelayMeasure();
        refresh();
    });
    side->addWidget(tests);

    auto* corr = new QGroupBox(tr("Correction file"));
    auto* cg = new QHBoxLayout(corr);
    m_save = new QPushButton(tr("Save..."));
    m_load = new QPushButton(tr("Load..."));
    m_save->setToolTip(tr("Save the present correction"));
    m_load->setToolTip(tr("Load a correction and apply it (Single mode)"));
    cg->addWidget(m_save);
    cg->addWidget(m_load);
    side->addWidget(corr);

    auto* stat = new QGroupBox(tr("Status"));
    auto* sv = new QVBoxLayout(stat);
    m_status = new QLabel;
    m_status->setTextFormat(Qt::RichText);
    m_status->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_status->setWordWrap(true);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    sv->addWidget(m_status);
    sv->addStretch(1);
    side->addWidget(stat, 1);

    auto* imdBox = new QGroupBox(tr("IMD (feedback, while transmitting 2 Tone)"));
    auto* iv = new QVBoxLayout(imdBox);
    m_imd = new QLabel(tr("Transmit 2 Tone with PS on to measure.<br><br><br><br>"));
    m_imd->setTextFormat(Qt::RichText);
    m_imd->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_imd->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    iv->addWidget(m_imd);
    auto* clr = new QPushButton(tr("Clear history"));
    iv->addWidget(clr);
    connect(clr, &QPushButton::clicked, this, [this] { m_spec->clearHistory(); });
    side->insertWidget(1, imdBox);

    // FFT tables (16384 points, 4-term Blackman-Harris).
    {
        const int n = anan::AnanPureSignal::kSnapPairs;
        m_win.resize(std::size_t(n));
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            const double a = 2.0 * kPi * i / (n - 1);
            const double w = 0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2 * a)
                             - 0.01168 * std::cos(3 * a);
            m_win[std::size_t(i)] = float(w);
            sum += w;
        }
        m_winSum = float(sum);
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        m_rev.resize(std::size_t(n));
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            m_rev[std::size_t(i)] = r;
        }
        m_re.resize(std::size_t(n));
        m_im.resize(std::size_t(n));
    }


    connect(m_twoTone, &QPushButton::toggled, this, [this](bool on) {
        if (m_applet)
            m_applet->requestTwoTone(on);
    });
    connect(m_cal, &QPushButton::clicked, this, [this] {
        if (auto* ps = m_applet ? m_applet->pureSignal() : nullptr) {
            ps->setMode(anan::AnanPureSignal::Mode::Single);
            ps->calibrate();
        }
    });
    connect(m_auto, &QPushButton::clicked, this, [this] {
        if (auto* ps = m_applet ? m_applet->pureSignal() : nullptr)
            ps->setMode(anan::AnanPureSignal::Mode::Auto);
        refresh();
    });
    connect(m_single, &QPushButton::clicked, this, [this] {
        if (auto* ps = m_applet ? m_applet->pureSignal() : nullptr)
            ps->setMode(anan::AnanPureSignal::Mode::Single);
        refresh();
    });
    connect(m_reset, &QPushButton::clicked, this, [this] {
        if (auto* ps = m_applet ? m_applet->pureSignal() : nullptr)
            ps->resetCorrection();
    });
    connect(m_save, &QPushButton::clicked, this, [this] {
        auto* ps = m_applet ? m_applet->pureSignal() : nullptr;
        if (!ps)
            return;
        const QString f = QFileDialog::getSaveFileName(this, tr("Save PureSignal correction"),
                                                       psDir(), tr("Correction (*.psc)"));
        if (!f.isEmpty())
            ps->saveCorrection(f);
    });
    connect(m_load, &QPushButton::clicked, this, [this] {
        auto* ps = m_applet ? m_applet->pureSignal() : nullptr;
        if (!ps)
            return;
        const QString f = QFileDialog::getOpenFileName(this, tr("Load PureSignal correction"),
                                                       psDir(), tr("Correction (*.psc);;All files (*)"));
        if (!f.isEmpty())
            ps->restoreCorrection(f);
    });

    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &PsAmpViewDialog::refresh);
    m_timer->start();
    resize(980, 760);
    refresh();
}

void PsAmpViewDialog::syncTwoTone(bool on)
{
    const QSignalBlocker b(m_twoTone);
    m_twoTone->setChecked(on);
}

void PsAmpViewDialog::refresh()
{
    anan::AnanPureSignal* ps = m_applet ? m_applet->pureSignal() : nullptr;
    const bool have = ps != nullptr;
    for (QWidget* w : {static_cast<QWidget*>(m_cal), static_cast<QWidget*>(m_auto),
                       static_cast<QWidget*>(m_single),
                       static_cast<QWidget*>(m_reset), static_cast<QWidget*>(m_save),
                       static_cast<QWidget*>(m_load)})
        w->setEnabled(have && ps->settings().enabled);
    const bool ttAllowed = m_applet && m_applet->twoToneAllowed();
    m_twoTone->setEnabled(have && (ttAllowed || m_twoTone->isChecked()));
    if (!have) {
        m_status->setText(tr("<b>No radio connected.</b>"));
        return;
    }
    const auto st = ps->status();
    if (st.feeding)
        measure();
    {
        const bool single = ps->settings().mode == anan::AnanPureSignal::Mode::Single;
        const QSignalBlocker b1(m_auto), b2(m_single), b3(m_cal);
        m_auto->setChecked(!single);
        m_single->setChecked(single);
        m_cal->setChecked(st.enabled && st.calibrating);
        m_measDelay->setEnabled(st.enabled && st.measStep < 0);
    }
    const int si = std::clamp(st.state, 0, 9);
    QString fbColor = QStringLiteral("#e03030");
    if (st.feedbackLevel > 181)
        fbColor = QStringLiteral("#3090ff");
    else if (st.feedbackLevel > 128)
        fbColor = QStringLiteral("#30e030");
    else if (st.feedbackLevel > 90)
        fbColor = QStringLiteral("#e0d020");
    auto num = [](double v) { return std::isnan(v) ? QStringLiteral("--") : QString::number(v, 'f', 1); };
    const QString green = QStringLiteral("#30e030"), red = QStringLiteral("#ff6060");
    QString t;
    t += tr("PureSignal <b>%1</b> (%2), state <b>%3</b><br>")
             .arg(st.enabled ? tr("ON") : tr("off"),
                  ps->settings().mode == anan::AnanPureSignal::Mode::Auto ? tr("Auto") : tr("Single"),
                  QLatin1String(kStateNames[si]));
    t += tr("Correcting <b>%1</b>, calibrations <b>%2</b>, check %3<br>")
             .arg(st.correcting ? tr("yes") : tr("no"))
             .arg(st.calibrations)
             .arg(st.info[6] == 0
                      ? QStringLiteral("<span style='color:%1'>OK</span>").arg(green)
                      : QStringLiteral("<span style='color:%1'>%2</span>")
                            .arg(red, anan::AnanPureSignal::scheckText(st.info[6]).toHtmlEscaped()));
    t += tr("Feedback ADC%5 level <b><span style='color:%1'>%2</span></b> (held at 152), attenuator <b>%3 dB</b>%4<br>")
             .arg(fbColor).arg(st.feedbackLevel).arg(st.attDb)
             .arg(ps->settings().autoAttenuate ? tr(" auto") : QString())
             .arg(ps->settings().feedbackAdc + 1);
    t += tr("HW peak <b>%1</b>%2<br>")
             .arg(st.hwPeak, 0, 'f', 4)
             .arg(st.hwMeasured > 0.0 ? tr(", measured %1").arg(st.hwMeasured, 0, 'f', 4) : QString());
    const QString amber = QStringLiteral("#e0b020");
    QString delayNote;
    if (st.measStep >= 0)
        delayNote = tr(", <b>measuring %1/%2</b> (correction off, peak gain %3 dB)")
                        .arg(st.measStep).arg(st.measSteps).arg(num(st.compressionDb));
    else if (!st.measResult.isEmpty())
        delayNote = QStringLiteral(", <span style='color:%1'>%2</span>")
                        .arg(st.measOk ? green : red, st.measResult.toHtmlEscaped());
    else if (st.delayMeasured)
        delayNote = tr(", measured %1").arg(st.delayMeasuredAt.toHtmlEscaped());
    else
        delayNote = QStringLiteral(", <span style='color:%1'>%2</span>")
                        .arg(amber, tr("default: Measure delay at low power"));
    t += tr("Amp delay%1 <b>%2 ns</b>%3<br>")
             .arg(QStringLiteral(" (") + (st.band.isEmpty() ? QString() : st.band.toHtmlEscaped() + QStringLiteral(", "))
                  + QStringLiteral("ADC%1)").arg(ps->settings().feedbackAdc + 1))
             .arg(st.effDelayNs, 0, 'f', 0)
             .arg(delayNote + tr(" (measured %1 + heat drift %2 ns, heat %3)")
                                  .arg(ps->settings().ampDelayNs, 0, 'f', 0)
                                  .arg(st.heatDriftNs, 0, 'f', 0)
                                  .arg(st.heat, 0, 'f', 2));
    t += tr("IMD3 sent <b>%1</b> / DAC <b>%2</b> / output <b>%3</b> dBc")
             .arg(num(st.imd3Host), num(st.imd3Ref), num(st.imd3Fb));
    // calcc health this transmission: failed calibrations and the corrections
    // calcc dropped by itself (each drop = the recalibration you see).
    if (st.calcFails || st.calcDrops) {
        t += tr("<br>calcc: failed calibrations <span style='color:%1'><b>%2</b></span>, "
                "correction dropped <span style='color:%1'><b>%3</b></span>")
                 .arg(red).arg(st.calcFails).arg(st.calcDrops);
        if (!st.lastCalcFail.isEmpty())
            t += tr("<br>last failure: %1").arg(st.lastCalcFail.toHtmlEscaped());
    }
    m_status->setText(t);

    // Curves after every new calibration (or the first time).
    if (m_hold->isChecked() || (st.calibrations == m_lastCal && m_lastCal >= 0))
        return;
    m_lastCal = st.calibrations;
    anan::AnanPureSignal::Disp d;
    if (!ps->display(d))
        return;
    PsCurveView::Curves c;
    const std::size_t n = d.x.size();
    double maxIn = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        maxIn = std::max(maxIn, d.x[i] * d.ym[i]);
    // Phase reference: the mean phase of the strongest 5% of the samples.
    double refSum = 0.0;
    int refN = 0;
    std::vector<double> phs(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        // calcc's ys is Im(tx * conj(rx)): the amplifier's own phase shift
        // (rx relative to tx) is minus its angle. Verified on a simulated PA:
        // the correction phase then mirrors this curve.
        phs[i] = -180.0 / kPi * std::atan2(d.ys[i], d.yc[i]);
        if (d.x[i] * d.ym[i] >= 0.95 * maxIn) {
            refSum += phs[i];
            ++refN;
        }
    }
    const double ref = refN ? refSum / refN : 0.0;
    const std::size_t stride = std::max<std::size_t>(1, n / 2048);
    for (std::size_t i = 0; i < n; i += stride) {
        const double in = d.x[i] * d.ym[i];
        if (!std::isfinite(in))
            continue;
        c.amAm.emplace_back(in, d.x[i]);
        double ph = phs[i] - ref;
        while (ph > 180.0) ph -= 360.0;
        while (ph < -180.0) ph += 360.0;
        c.amPm.emplace_back(in, ph);
    }
    for (std::size_t k = 0; k < d.xm.size(); ++k)
        c.corrMag.emplace_back(d.xm[k], d.xm[k] * d.ymc[k]);
    for (std::size_t k = 0; k < d.xa.size(); ++k)
        c.corrPhs.emplace_back(d.xa[k], d.yac[k]);
    m_view->setCurves(c);
}

// ── IMD measurement ──────────────────────────────────────────────────────────

void PsAmpViewDialog::measure()
{
    anan::AnanPureSignal* ps = m_applet ? m_applet->pureSignal() : nullptr;
    if (!ps || !ps->feedbackSnapshot(m_snap))
        return;
    const int n = anan::AnanPureSignal::kSnapPairs;
    constexpr double kFs = 192000.0;
    // No mean is removed (02.11). The plain mean of a snapshot whose tones do
    // not fit a whole number of cycles is the tones' leakage (~-50 dBc), and
    // subtracting it put a false -46..-48 dBc line at 0 Hz -- the "stick"
    // left of the tones. The Hann window alone keeps the tones out of bin 0
    // (< -115 dBc), so what shows at 0 Hz now is a real carrier.
    for (int i = 0; i < n; ++i) {
        const int r = m_rev[std::size_t(i)];
        m_re[std::size_t(r)] = m_snap[std::size_t(2 * i)] * m_win[std::size_t(i)];
        m_im[std::size_t(r)] = m_snap[std::size_t(2 * i + 1)] * m_win[std::size_t(i)];
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / len;
        const double wr = std::cos(ang), wi = std::sin(ang);
        const int half = len >> 1;
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (int j = 0; j < half; ++j) {
                const std::size_t a = std::size_t(i + j), b = std::size_t(i + j + half);
                const double tr = m_re[b] * cr - m_im[b] * ci;
                const double ti = m_re[b] * ci + m_im[b] * cr;
                m_re[b] = m_re[a] - tr;
                m_im[b] = m_im[a] - ti;
                m_re[a] += tr;
                m_im[a] += ti;
                const double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
    const double binHz = kFs / n;
    // Power per bin, centred: index c is 0 Hz.
    auto power = [&](int k) {   // k: signed bin
        const int idx = ((k % n) + n) % n;
        return m_re[std::size_t(idx)] * m_re[std::size_t(idx)]
               + m_im[std::size_t(idx)] * m_im[std::size_t(idx)];
    };
    const int span = int(5000.0 / binHz);
    // Strongest bin (not DC), then the strongest at least 300 Hz away.
    int k1 = 0;
    double p1 = -1.0;
    for (int k = -span; k <= span; ++k) {
        if (std::abs(k) * binHz < 100.0)
            continue;
        if (power(k) > p1) {
            p1 = power(k);
            k1 = k;
        }
    }
    int k2 = 0;
    double p2 = -1.0;
    for (int k = -span; k <= span; ++k) {
        if (std::abs(k) * binHz < 100.0 || std::abs(k - k1) * binHz < 200.0)
            continue;
        if (power(k) > p2) {
            p2 = power(k);
            k2 = k;
        }
    }
    if (p1 <= 0.0 || p2 <= 0.0 || p2 < p1 * 0.05)   // not a two-tone
        return;
    if (k2 < k1)
        std::swap(k1, k2);
    const int d = k2 - k1;
    auto band = [&](int k) {   // energy of a line: +-4 bins around its peak
        double e = 0.0;
        for (int j = -4; j <= 4; ++j)
            e += power(k + j);
        return e;
    };
    const double tone = 0.5 * (band(k1) + band(k2));
    auto dbc = [&](int k) { return 10.0 * std::log10(std::max(band(k), 1e-30) / tone); };
    const double imd3 = std::max(dbc(k1 - d), dbc(k2 + d));
    const double imd5 = std::max(dbc(k1 - 2 * d), dbc(k2 + 2 * d));
    const double imd7 = std::max(dbc(k1 - 3 * d), dbc(k2 + 3 * d));
    const double imd9 = std::max(dbc(k1 - 4 * d), dbc(k2 + 4 * d));
    // Noise near the tones (median of the span), relative to a tone.
    std::vector<double> tmp;
    tmp.reserve(std::size_t(2 * span + 1));
    for (int k = -span; k <= span; ++k)
        tmp.push_back(power(k));
    std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 2, tmp.end());
    const double floorDbc = 10.0 * std::log10(std::max(tmp[tmp.size() / 2] * 9.0, 1e-30) / tone);

    // The carrier: bin 0 (+-1 bin, the window's main lobe), and the strongest
    // other line within 150 Hz (a carrier that is offset or moves).
    const double toneBin = 0.5 * (power(k1) + power(k2));
    const double dcDbc = 10.0 * std::log10(
        std::max(std::max(power(0), std::max(power(-1), power(1))), 1e-30) / toneBin);
    int kc = 0;
    double pc = -1.0;
    for (int k = -int(150.0 / binHz); k <= int(150.0 / binHz); ++k)
        if (std::abs(k) > 2 && power(k) > pc) {
            pc = power(k);
            kc = k;
        }
    const double lineDbc = 10.0 * std::log10(std::max(pc, 1e-30) / toneBin);
    m_imd->setText(tr("IMD3 <b>%1</b> dBc (lower %8, upper %9)<br>"
                      "IMD5 <b>%2</b> &nbsp; IMD7 <b>%3</b> &nbsp; IMD9 <b>%4</b> dBc<br>"
                      "tones %5 / %6 Hz, noise %7 dBc<br>"
                      "carrier (0 Hz) <b>%10</b> dBc; strongest line within 150 Hz %11 dBc at %12 Hz<br>"
                      "<small>relative to one tone; worse side of each pair</small>")
                       .arg(imd3, 0, 'f', 1).arg(imd5, 0, 'f', 1)
                       .arg(imd7, 0, 'f', 1).arg(imd9, 0, 'f', 1)
                       .arg(std::abs(k1 * binHz), 0, 'f', 0).arg(std::abs(k2 * binHz), 0, 'f', 0)
                       .arg(floorDbc, 0, 'f', 0)
                       .arg(dbc(k1 - d), 0, 'f', 1).arg(dbc(k2 + d), 0, 'f', 1)
                       .arg(dcDbc, 0, 'f', 1).arg(lineDbc, 0, 'f', 1).arg(kc * binHz, 0, 'f', 0));
    PsSpectrumView::Frame f;
    const int lo = k1 - 5 * d, hi = k2 + 5 * d;
    f.f0 = lo * binHz;
    f.binHz = binHz;
    f.f1 = k1 * binHz;
    f.f2 = k2 * binHz;
    f.valid = true;
    const double toneLine = 0.5 * (power(k1) + power(k2));
    f.dbc.reserve(std::size_t(hi - lo + 1));
    for (int k = lo; k <= hi; ++k)
        f.dbc.push_back(float(10.0 * std::log10(std::max(power(k), 1e-30) / toneLine)));
    m_spec->setFrame(f);
    m_spec->addHistory(imd3, imd5);
}

// ── Spectrum view ────────────────────────────────────────────────────────────

PsSpectrumView::PsSpectrumView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(460, 220);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void PsSpectrumView::setFrame(const Frame& f)
{
    m_f = f;
    update();
}

void PsSpectrumView::addHistory(double imd3, double imd5)
{
    m_hist.emplace_back(imd3, imd5);
    if (m_hist.size() > 120)
        m_hist.erase(m_hist.begin());
    update();
}

void PsSpectrumView::clearHistory()
{
    m_hist.clear();
    update();
}

void PsSpectrumView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(10, 14, 20));
    QFont font = this->font();
    font.setPixelSize(10);
    p.setFont(font);
    const int histH = std::max(60, height() / 3);
    const QRect sr = QRect(46, 8, width() - 56, height() - histH - 30);
    const QRect hr = QRect(46, sr.bottom() + 22, width() - 56, histH - 14);
    p.fillRect(sr, QColor(4, 8, 14));
    p.fillRect(hr, QColor(4, 8, 14));

    constexpr double kTop = 5.0, kBot = -100.0;
    auto Ys = [&](double db) {
        return sr.top() + (kTop - std::clamp(db, kBot, kTop)) / (kTop - kBot) * sr.height();
    };
    for (int db = 0; db >= -100; db -= 20) {
        p.setPen(QColor(36, 46, 58));
        p.drawLine(QPointF(sr.left(), Ys(db)), QPointF(sr.right(), Ys(db)));
        p.setPen(QColor(150, 160, 175));
        p.drawText(QRectF(0, Ys(db) - 7, 42, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(db));
    }
    if (m_f.valid && m_f.dbc.size() > 1) {
        const double fLo = m_f.f0, fHi = m_f.f0 + (m_f.dbc.size() - 1) * m_f.binHz;
        auto X = [&](double hz) { return sr.left() + (hz - fLo) / (fHi - fLo) * sr.width(); };
        // IMD product markers.
        const double d = m_f.f2 - m_f.f1;
        for (int k = 1; k <= 4; ++k) {
            for (double hz : {m_f.f1 - k * d, m_f.f2 + k * d}) {
                p.setPen(QPen(QColor(90, 60, 60), 1, Qt::DotLine));
                p.drawLine(QPointF(X(hz), sr.top()), QPointF(X(hz), sr.bottom()));
                p.setPen(QColor(200, 120, 120));
                p.drawText(QRectF(X(hz) - 20, sr.bottom() + 2, 40, 12), Qt::AlignCenter,
                           QStringLiteral("IMD%1").arg(2 * k + 1));
            }
        }
        QPolygonF poly;
        poly.reserve(int(m_f.dbc.size()));
        for (std::size_t i = 0; i < m_f.dbc.size(); ++i)
            poly << QPointF(X(fLo + double(i) * m_f.binHz), Ys(m_f.dbc[i]));
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(0, 200, 255), 1));
        p.drawPolyline(poly);
        p.setRenderHint(QPainter::Antialiasing, false);
    } else {
        p.setPen(QColor(150, 160, 175));
        p.drawText(sr, Qt::AlignCenter, tr("Feedback spectrum: transmit 2 Tone with PS on"));
    }

    // IMD3 / IMD5 history (last 120 measurements, 4 per second).
    constexpr double hTop = -10.0, hBot = -80.0;
    auto Yh = [&](double db) {
        return hr.top() + (hTop - std::clamp(db, hBot, hTop)) / (hTop - hBot) * hr.height();
    };
    for (int db = -20; db >= -80; db -= 20) {
        p.setPen(QColor(36, 46, 58));
        p.drawLine(QPointF(hr.left(), Yh(db)), QPointF(hr.right(), Yh(db)));
        p.setPen(QColor(150, 160, 175));
        p.drawText(QRectF(0, Yh(db) - 7, 42, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(db));
    }
    p.setPen(QColor(150, 160, 175));
    p.drawText(QRectF(hr.left(), hr.top() - 14, hr.width(), 12), Qt::AlignLeft,
               tr("IMD3 (red) / IMD5 (orange), dBc, last 30 s"));
    if (m_hist.size() > 1) {
        auto Xh = [&](std::size_t i) { return hr.left() + double(i) / 119.0 * hr.width(); };
        QPolygonF a, b;
        for (std::size_t i = 0; i < m_hist.size(); ++i) {
            a << QPointF(Xh(i), Yh(m_hist[i].x()));
            b << QPointF(Xh(i), Yh(m_hist[i].y()));
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(255, 80, 80), 1.5));
        p.drawPolyline(a);
        p.setPen(QPen(QColor(255, 170, 60), 1.5));
        p.drawPolyline(b);
    }
}

} // namespace AetherSDR
