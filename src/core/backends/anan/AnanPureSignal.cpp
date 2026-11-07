#include "core/backends/anan/AnanPureSignal.h"
#include "core/dsp/WdspTxaGuard.h"

#include "core/AppSettings.h"
#include "core/dsp/WdspPureSignal.h"
#include "core/backends/anan/AnanTwoTone.h"
#include "core/backends/anan/P2Transport.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

Q_LOGGING_CATEGORY(lcAnanPs, "aether.anan.ps")

namespace AetherSDR::anan {

namespace {

constexpr int kFeedbackRateHz = 192000;
// Thetis puresignal.NeedToRecalibrate / IsFeedbackLevelOKRange.
constexpr double kFbTarget = 152.293;
// The coarse limits (Thetis' reset-and-jump), see the auto-attenuate state
// machine.
constexpr int kFbCoarseLow = 100;
constexpr int kFbCoarseHigh = 200;
// The Thetis window (puresignal.NeedToRecalibrate: > 181, or <= 128 with
// attenuation left). Inside it calcc works as well as at 152, so the
// smoothed level is left alone there; once it leaves, the attenuator jumps
// back to 152 in one step, the correction kept (anan157: the +-0.6 dB hold
// of anan143.. stepped six times in 12 s on a drive ramp, log 04.11 17:30,
// and every step left a mixed-level collection behind -- scheck failed
// after each one, twice three in a row).
constexpr double kFbWinLow = 128.0;
constexpr double kFbWinHigh = 181.0;
// anan161 (operator, 04.11: "just switch that check off and see"): WDSP
// 2.10's scheck compares each solution with the previous one (|d| <= 0.10
// at 40 points). On this PA (low bias, floating gates: the bias rises with
// power) the correction at x = 0.026 moves by 0.11..0.45 per calibration;
// all 29 failures of log 18:06 were that point and three in a row dropped
// the correction. Off, the comparison is still logged ("would have failed").
// anan163 (log 18:47): with the check off, the magnitude correction at
// x = 0.026 went NEGATIVE (-1.4 .. -2.4, 18:47:42..47) and was blended
// into the curves; the AM/AM fit then failed four times in a row and the
// correction was dropped for 17 s. Thetis' classic calcc does both things
// the off-mode lacked and the upstream mode overdid: it compares solutions
// only from its 5th of 16 intervals up (x >= 0.25) and rejects a negative
// corrected drive. Mode 2 of WDSP patch 28 is exactly that.
constexpr int kScheckMode = 2;
// anan162 (operator, 04.11: "you know roughly how the delay changes with
// the transmit time -- pick a time law for a smooth correction"). One over
// at drive 108 (log 18:34): Measure delay at 2 s 3010..3190 ns, at 45 s
// 3740..3870, at 75 s 4010..4056 -- least squares: d(t) = 4235 - 1205 *
// exp(-t / 42 s). Modelled as a heat state h: towards u = (drive / 108)^2
// (the dissipation) with 42 s while transmitting, back to 0 with
// kCoolTauS while receiving (not measured yet: after 8 min of rest the
// over started at the cold value). The delay applied is the band's
// measured one plus 1205 ns * (h - h at the measurement), moved smoothly
// (SetPSTXDelay only re-indexes the WDSP delay line; no reset of the
// learning).
constexpr double kHeatTauS = 42.0;
constexpr double kCoolTauS = 90.0;
constexpr double kHeatDelayNs = 1205.0;
constexpr double kHeatRefDrive = 108.0;
constexpr double kHeatApplyStepNs = 10.0;
// Drive feed-forward: P2Transport::setTxDriveLevel (anan160) moves the
// attenuator in the same instant as the drive; the tick adopts it.
// A glitch (DUC zeros / a TX-buffer error): blocks of 1024 pairs not given
// to calcc (40 = 0.21 s), and how long the attenuator tracking waits.

long long nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

// ANAN-100D port (operator, 01.11: "replace our test controls by known
// constants", then "use the measured delay and HW peak by default -- full
// automation"): nothing to set by hand.
//   HW peak starts at 0.2899 (Thetis cmaster.cs, P2 with the CFIR) and
//   follows the measured DDC1 / DUC scale x CFIR gain; amp delay starts at
//   150 ns (Thetis PSForm) until "Measure delay" has fixed it -- once, at low
//   power with the correction off (saved, not re-estimated while correcting); MOX
//   delay 0.2 s; the pairs used as received; the Thetis two-tone.
constexpr double kHwPeak = 0.2899;
constexpr double kMoxDelaySec = 0.2;
constexpr double kAmpDelayNs = 600.0;   // a band not measured yet (ANAN-100D, 01.11: 600..1400 measured)

// Amp delay memory per amateur band (the coupler sits before the band
// filters, but the operator keeps one value per band anyway).
QString bandFor(double hz)
{
    struct B { double lo, hi; const char* name; };
    static constexpr B kBands[] = {
        {1.8e6, 2.0e6, "160 m"},     {3.5e6, 4.0e6, "80 m"},      {5.25e6, 5.45e6, "60 m"},
        {7.0e6, 7.3e6, "40 m"},      {10.1e6, 10.15e6, "30 m"},   {14.0e6, 14.35e6, "20 m"},
        {18.068e6, 18.168e6, "17 m"}, {21.0e6, 21.45e6, "15 m"},  {24.89e6, 24.99e6, "12 m"},
        {28.0e6, 29.7e6, "10 m"},    {50.0e6, 54.0e6, "6 m"},
    };
    for (const B& b : kBands)
        if (hz >= b.lo && hz <= b.hi)
            return QString::fromLatin1(b.name);
    return QStringLiteral("%1 MHz").arg(int(hz / 1.0e6));   // outside the bands: per MHz
}

QString bandKey(const QString& band)
{
    QString k = band;
    k.remove(QLatin1Char(' '));
    return k;   // "40m", "7MHz"
}

void forceConstants(AnanPureSignal::Settings& s)
{
    s.moxDelaySec = kMoxDelaySec;
    s.autoHwPeak = true;
    s.conjugate = false;
    s.ttFreq1 = 700.0;   // Thetis two-tone defaults (setup.cs)
    s.ttFreq2 = 1900.0;
    s.ttMag = 0.49;
}

// Passband (low-frequency) gain of WDSP's TXA CFIR as TXA.c builds it for
// Protocol 2 (cfir.c cfir_impulse: CIC R 640, 5 pairs, cutoff 20 kHz at the
// 192 kHz CIC input): 1 / droop(cutoff)^5 = 0.9143. The CFIR sits between the
// PureSignal correction and the DUC, so the HW peak (DDC1 amplitude per unit
// at the correction) is the measured DDC1 / DUC ratio times this -- with it,
// the measurement gives 0.296 against Thetis' fixed 0.2899.
double cfirPassbandGain()
{
    constexpr double kPi = 3.14159265358979323846;
    const double ft = 20000.0 / 192000.0;
    const double r = 640.0;
    const double droop = r * std::sin(kPi * ft / r) / std::sin(kPi * ft);
    return 1.0 / std::pow(droop, 5.0);
}

bool tfSetting(const char* key, bool def)
{
    return AppSettings::instance()
               .value(QLatin1String(key), def ? QStringLiteral("True") : QStringLiteral("False"))
               .toString() == QLatin1String("True");
}

} // namespace

AnanPureSignal::AnanPureSignal(QObject* parent)
    : QObject(parent)
{
    load();
    m_accTx.assign(2 * kBlock, 0.0);
    m_accRx.assign(2 * kBlock, 0.0);
    m_snap.assign(2 * kSnapPairs, 0.0);
    m_snapTx.assign(2 * kSnapPairs, 0.0);
    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, &AnanPureSignal::tick);
    m_timer->start();
}

AnanPureSignal::~AnanPureSignal() = default;

void AnanPureSignal::load()
{
    auto& st = AppSettings::instance();
    m_s.enabled = tfSetting("AnanPsEnabled", false);
    m_s.mode = st.value(QStringLiteral("AnanPsMode"), 0).toInt() == 1 ? Mode::Single : Mode::Auto;
    m_s.feedbackSource = std::clamp(st.value(QStringLiteral("AnanPsSource"), 0).toInt(), 0, 2);
    m_s.autoAttenuate = tfSetting("AnanPsAutoAtt", true);
    m_s.attDb = std::clamp(st.value(QStringLiteral("AnanPsAttDb"), 20).toInt(), 0, 31);
    m_s.hwPeak = std::clamp(st.value(QStringLiteral("AnanPsHwPeak"), 0.2899).toDouble(), 0.01, 2.0);
    m_s.autoHwPeak = tfSetting("AnanPsAutoHwPeak", false);
    // Settings v2 (01.11): the TXA CFIR now runs as in Thetis, so the HW peak
    // is Thetis' 0.2899 again; the value the old automatic measurement stored
    // (DDC1 / DUC without the CFIR, ~0.324) is dropped once, and the automatic
    // follow is off by default (it is a measurement, not a setting).
    if (st.value(QStringLiteral("AnanPsCfgVer"), 1).toInt() < 2) {
        m_s.hwPeak = 0.2899;
        m_s.autoHwPeak = false;
        st.setValue(QStringLiteral("AnanPsCfgVer"), 2);
        st.setValue(QStringLiteral("AnanPsHwPeak"), m_s.hwPeak);
        st.setValue(QStringLiteral("AnanPsAutoHwPeak"), QStringLiteral("False"));
        st.save();
    }
    m_s.moxDelaySec = std::clamp(st.value(QStringLiteral("AnanPsMoxDelay"), 0.2).toDouble(), 0.0, 10.0);
    m_s.ampDelayNs = std::clamp(st.value(QStringLiteral("AnanPsAmpDelayNs"), 150.0).toDouble(), -1000000.0, 1000000.0);
    m_s.ttFreq1 = std::clamp(st.value(QStringLiteral("AnanPsTtFreq1"), 700.0).toDouble(), 50.0, 5000.0);
    m_s.ttFreq2 = std::clamp(st.value(QStringLiteral("AnanPsTtFreq2"), 1900.0).toDouble(), 50.0, 5000.0);
    m_s.ttMag = std::clamp(st.value(QStringLiteral("AnanPsTtMag"), 0.49).toDouble(), 0.01, 0.5);
    forceConstants(m_s);
    m_s.hwPeak = kHwPeak;       // each session starts from the default; the measurement takes over
    // Amp delay: the one "Measure delay" found (low power, correction off),
    // else Thetis' 150 ns. The values the earlier automatic follow saved in
    // AnanPsAmpDelayNs were measured with the correction on (DDC1 = the
    // predistorted signal: the estimate drifted 1943 -> 2358 ns as the
    // correction came in, log 21:30) and are not taken over.
    // Per band since 01.11 22:20 (AnanPsDelayNs_<band>); until the first
    // transmit frequency arrives the default. The single value of anan128
    // (AnanPsDelayMeasuredNs) is not taken over -- it has no band.
    m_s.ampDelayNs = kAmpDelayNs;
    m_s.feedbackAdc = std::clamp(st.value(QStringLiteral("AnanPsFeedbackAdc"), 0).toInt(), 0, 1);
    m_s.attDbByAdc[0] = m_s.attDb;   // AnanPsAttDb: ADC1, as before
    m_s.attDbByAdc[1] = std::clamp(st.value(QStringLiteral("AnanPsAttDbAdc2"), 20).toInt(), 0, 31);
    m_s.attDb = m_s.attDbByAdc[std::size_t(m_s.feedbackAdc)];
    m_conjugate.store(m_s.conjugate);
}

void AnanPureSignal::save() const
{
    auto& st = AppSettings::instance();
    st.setValue(QStringLiteral("AnanPsEnabled"), m_s.enabled ? QStringLiteral("True") : QStringLiteral("False"));
    st.setValue(QStringLiteral("AnanPsMode"), m_s.mode == Mode::Single ? 1 : 0);
    st.setValue(QStringLiteral("AnanPsSource"), m_s.feedbackSource);
    st.setValue(QStringLiteral("AnanPsFeedbackAdc"), m_s.feedbackAdc);
    st.setValue(QStringLiteral("AnanPsAutoAtt"), m_s.autoAttenuate ? QStringLiteral("True") : QStringLiteral("False"));
    st.setValue(QStringLiteral("AnanPsAttDb"), m_s.feedbackAdc == 0 ? m_s.attDb : m_s.attDbByAdc[0]);
    st.setValue(QStringLiteral("AnanPsAttDbAdc2"), m_s.feedbackAdc == 1 ? m_s.attDb : m_s.attDbByAdc[1]);
    st.setValue(QStringLiteral("AnanPsHwPeak"), m_s.hwPeak);
    st.setValue(QStringLiteral("AnanPsAutoHwPeak"), m_s.autoHwPeak ? QStringLiteral("True") : QStringLiteral("False"));
    st.setValue(QStringLiteral("AnanPsMoxDelay"), m_s.moxDelaySec);
    st.setValue(QStringLiteral("AnanPsAmpDelayNs"), m_s.ampDelayNs);
    st.setValue(QStringLiteral("AnanPsTtFreq1"), m_s.ttFreq1);
    st.setValue(QStringLiteral("AnanPsTtFreq2"), m_s.ttFreq2);
    st.setValue(QStringLiteral("AnanPsTtMag"), m_s.ttMag);
    st.setValue(QStringLiteral("AnanPsConjugate"), m_s.conjugate ? QStringLiteral("True") : QStringLiteral("False"));
    st.save();
}

void AnanPureSignal::pushTransport()
{
    if (!m_transport)
        return;
    QMetaObject::invokeMethod(m_transport, "setPureSignal", Qt::QueuedConnection,
                              Q_ARG(bool, m_s.enabled), Q_ARG(int, m_s.feedbackSource),
                              Q_ARG(int, m_s.attDb));
    QMetaObject::invokeMethod(m_transport, "setPsFeedbackAdc", Qt::QueuedConnection,
                              Q_ARG(int, m_s.feedbackAdc));
    QMetaObject::invokeMethod(m_transport, "setPsAttFollowsDrive", Qt::QueuedConnection,
                              Q_ARG(bool, m_s.enabled && m_s.autoAttenuate));
}

void AnanPureSignal::applyWdspParams()
{
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (!txaHold)
        return;
    if (m_channel < 0)
        return;
#if AETHER_WDSP_PS_CLASSIC
    static bool logged = false;
    if (!logged) {
        logged = true;
        qCInfo(lcAnanPs) << "PureSignal engine: Thetis classic calcc/iqc";
    }
#else
    static bool logged = false;
    if (!logged) {
        logged = true;
        qCInfo(lcAnanPs) << "PureSignal engine: WDSP 2.10 calcc/iqc, scheck"
                         << (kScheckMode == 2 ? "as Thetis (x >= 0.25, magnitude >= 0)"
                                              : kScheckMode == 1 ? "OFF (logged only)" : "upstream");
    }
    SetPSScheckMode(m_channel, kScheckMode);
#endif
    SetPSFeedbackRate(m_channel, kFeedbackRateHz);
    SetPSHWPeak(m_channel, m_s.hwPeak);
    SetPSMoxDelay(m_channel, m_s.moxDelaySec);
    m_delayAppliedNs = effectiveDelayNs();
    SetPSTXDelay(m_channel, 1.0e-9 * m_delayAppliedNs);
    SetPSLoopDelay(m_channel, 0.0);
}

void AnanPureSignal::control(int reset, int mancal, int automode, int turnon)
{
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (!txaHold)
        return;
    if (m_channel < 0)
        return;
    if (reset)
        m_ownResetMs = nowMs();   // ours, not a calcc decision (tick() logs calcc's own)
    SetPSControl(m_channel, reset, mancal, automode, turnon);
}

void AnanPureSignal::setTxChannel(int channel, unsigned generation)
{
    // The TX DSP re-creates its channel with the same id on a (re)connect:
    // the generation tells a fresh calcc from the old one (WdspTxaGuard.h).
    if (channel == m_channel && generation == m_channelGen)
        return;
    m_channel = channel;
    m_channelGen = generation;
    m_feedChannel.store(channel, std::memory_order_release);
    if (channel < 0)
        return;
    qCInfo(lcAnanPs) << "PureSignal: TXA channel" << channel << "generation" << generation
                     << (m_s.enabled ? "(PS on)" : "(PS off)");
    applyWdspParams();
    if (m_s.enabled)
        control(1, m_s.mode == Mode::Single ? 1 : 0, m_s.mode == Mode::Auto ? 1 : 0, 0);
    pushTransport();
    updateFeedGate();
}

void AnanPureSignal::updateFeedGate()
{
    const bool ok = m_s.enabled && m_keyed && !m_cw && m_channel >= 0;
    m_feedOk.store(ok, std::memory_order_release);
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (txaHold && m_channel >= 0)
        SetPSMox(m_channel, ok ? 1 : 0);
}

void AnanPureSignal::setKeyed(bool keyed, bool cw)
{
    if (keyed == m_keyed && cw == m_cw)
        return;
    if (keyed && !m_keyed) {
#if !AETHER_WDSP_PS_CLASSIC
        const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
        if (txaHold && m_channel >= 0)
            SetPSScheckMode(m_channel, kScheckMode);   // a re-created calcc starts in mode 0
#endif
        m_calcFails = 0;
        m_calcDrops = 0;
        m_lastCalcFail.clear();
    }
    m_keyed = keyed;
    m_cw = cw;
    updateFeedGate();
}

void AnanPureSignal::feed(const double* tx, const double* rx, int n, double hwPeak)
{
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (!txaHold)
        return;
    if (hwPeak > 0.0) {
        hwPeak *= cfirPassbandGain();   // DDC1 / DUC -> DDC1 per unit at the correction
        m_hwMeasured.store(hwPeak, std::memory_order_relaxed);
        m_hwMeasuredMs.store(nowMs(), std::memory_order_relaxed);
    }
    // Radio I/O thread.
    if (!m_feedOk.load(std::memory_order_acquire)) {
        if (m_accN || m_snapFill) {
            std::lock_guard<std::mutex> lock(m_snapMutex);
            m_snapFill = 0;
        }
        m_accN = 0;
        return;
    }
    // The pairs arrive in the host IQ's handedness (as calcc needs them); the
    // conjugate is only a diagnostic (see Settings::conjugate).
    if (m_conjugate.load(std::memory_order_relaxed)) {
        m_flipTx.assign(tx, tx + 2 * n);
        m_flipRx.assign(rx, rx + 2 * n);
        for (int i = 0; i < n; ++i) {
            m_flipTx[std::size_t(2 * i + 1)] = -m_flipTx[std::size_t(2 * i + 1)];
            m_flipRx[std::size_t(2 * i + 1)] = -m_flipRx[std::size_t(2 * i + 1)];
        }
        tx = m_flipTx.data();
        rx = m_flipRx.data();
    }
    {
        std::lock_guard<std::mutex> lock(m_snapMutex);
        for (int i = 0; i < n; ++i) {
            m_snap[std::size_t(2 * m_snapPos)] = rx[2 * i];
            m_snap[std::size_t(2 * m_snapPos + 1)] = rx[2 * i + 1];
            m_snapTx[std::size_t(2 * m_snapPos)] = tx[2 * i];
            m_snapTx[std::size_t(2 * m_snapPos + 1)] = tx[2 * i + 1];
            m_snapPos = (m_snapPos + 1) % kSnapPairs;
        }
        m_snapFill = std::min(kSnapPairs, m_snapFill + n);
    }
    const int ch = m_feedChannel.load(std::memory_order_acquire);
    if (ch < 0)
        return;
    for (int i = 0; i < n; ++i) {
        m_accTx[std::size_t(2 * m_accN)] = tx[2 * i];
        m_accTx[std::size_t(2 * m_accN + 1)] = tx[2 * i + 1];
        m_accRx[std::size_t(2 * m_accN)] = rx[2 * i];
        m_accRx[std::size_t(2 * m_accN + 1)] = rx[2 * i + 1];
        if (++m_accN == kBlock) {
            // 03.11: the radio's TX stream is clean now (firmware fixed), and
            // other hardware reports nothing about it: every block is fed.
            // (The dropout / step / buffer-error pauses of 02.11 are gone.)
            pscc(ch, kBlock, m_accTx.data(), m_accRx.data());
            m_accN = 0;
        }
    }
    m_pairs.fetch_add(static_cast<unsigned long long>(n), std::memory_order_relaxed);
    m_lastFeedMs.store(nowMs(), std::memory_order_relaxed);
}

void AnanPureSignal::setEnabled(bool on)
{
    if (on == m_s.enabled)
        return;
    m_s.enabled = on;
    save();
    qCInfo(lcAnanPs) << "PureSignal" << (on ? "ON" : "off");
    if (on) {
        pushTransport();
        applyWdspParams();
        // Give the feedback stream a moment, then start the engine (deskHPSDR
        // tx_ps_onoff).
        QTimer::singleShot(100, this, [this] {
            if (m_s.enabled)
                control(1, m_s.mode == Mode::Single ? 1 : 0, m_s.mode == Mode::Auto ? 1 : 0, 0);
        });
    } else {
        control(1, 0, 0, 0);
        // The reset only takes effect while pscc runs (deskHPSDR): feed a few
        // blocks of zeros when nothing else is feeding.
        const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
        if (txaHold && m_channel >= 0 && !m_feedOk.load()) {
            std::vector<double> zero(2 * kBlock, 0.0);
            for (int i = 0; i < 7; ++i)
                pscc(m_channel, kBlock, zero.data(), zero.data());
        }
        if (m_twoTone)
            setTwoTone(false);
        pushTransport();
    }
    updateFeedGate();
    emit statusChanged();
}

void AnanPureSignal::setMode(Mode mode)
{
    if (mode == m_s.mode)
        return;
    m_s.mode = mode;
    save();
    if (m_s.enabled) {
        if (mode == Mode::Auto)
            control(0, 0, 1, 0);
        else
            control(0, 0, 0, 0);   // keep the present correction (Thetis StayON)
    }
    emit statusChanged();
}

void AnanPureSignal::calibrate()
{
    if (!m_s.enabled)
        return;
    if (m_singleRunning) {
        // Thetis btnPSCalibrate: a second click while the Single calibration
        // has not completed (calcc keeps rejecting it) gives up. The
        // correction in force, if any, is kept.
        m_singleRunning = false;
        control(0, 0, 0, 0);
        qCInfo(lcAnanPs) << "PureSignal: Single calibration cancelled";
        emit statusChanged();
        return;
    }
    m_singleRunning = true;
    control(1, 1, 0, 0);   // Thetis TurnOnSingleCalibrate
}

void AnanPureSignal::setLoopbackTest(bool on)
{
    m_loopback = on;
    m_delayLoggedNs = 1e9;
    if (m_transport)
        QMetaObject::invokeMethod(m_transport, "setPsLoopbackTest", Qt::QueuedConnection, Q_ARG(bool, on));
    emit statusChanged();
}

// anan156 (operator, 03.11): back to the manual measurement. The automatic
// delay of builds 154/155 re-estimated the lag while correcting (log 22:08:
// 1580 -> 910 -> ... -> 2060 ns in one over, the fit residual at the new and
// the old delay equal) -- a closed loop with calcc that restarted the
// learning on every change. The delay measured here at low power is stored
// per band and feedback ADC and used unchanged at any power.
// ANAN-100D port (01.11, FPGA developer's advice): the delay is a property
// of the path, not of the amplitude. Measure it once at low power, in the
// linear region, with the correction off (DDC1 = the clean signal), and fix
// it; everything that changes with the amplitude belongs to the AM/AM and
// AM/PM tables. A delay re-estimated while correcting compares the
// predistorted reference with the output and wanders with the power.
static constexpr int kMeasSteps = 5;
static constexpr long long kMeasSettleMs = 700;
static constexpr long long kMeasTimeoutMs = 20000;
static constexpr double kMeasMaxSpreadNs = 400.0;
// Only compression at the peak (saturation) is refused. A class-AB stage
// shows gain EXPANSION towards the peak -- lower gain at small envelope
// (crossover), the calcc ym curve 1.3..2.0 at 0..0.2 of the output, 1.0 at
// the peak -- and that is the same at any power (log 21:58: +2.0 dB at three
// drive levels), so it is not a sign of too much power and is only reported.
static constexpr double kMeasMaxCompDb = 1.5;
// anan157: no "fit must improve by 0.5 dB" rule any more. The measurement
// is the operator's explicit request, the readings are steady (spread
// limit), and that rule kept a wrong stored value (log 04.11 17:30: five
// readings 1572..1600 ns, the stored 2060 ns from the automatic delay of
// build 155 fitted only 0.4 dB worse and was kept).

bool AnanPureSignal::startDelayMeasure()
{
    if (m_measStep >= 0)
        return false;
    if (!m_s.enabled || !m_keyed || m_cw || !m_feedOk.load(std::memory_order_relaxed)) {
        m_measResult = tr("transmit first (2 Tone 700/1900 Hz or voice) at low power");
        m_measOk = false;
        emit statusChanged();
        return false;
    }
    if (m_twoTone && std::abs(m_s.ttFreq2 - m_s.ttFreq1) < 1000.0) {
        m_measResult = tr("use the 700/1900 Hz pair: a 300 Hz beat is too slow to time");
        m_measOk = false;
        emit statusChanged();
        return false;
    }
    m_measDelays.clear();
    m_measComps.clear();
    m_measFitDelays.clear();
    m_measFitRes.clear();
    m_measFitResOld.clear();
    m_fitDelayNs = m_fitResDb = m_fitResOldDb = NAN;
    m_measBand = m_band;
    m_measStep = 0;
    m_measStartMs = nowMs();
    m_measSeqSeen = m_delaySeq;
    m_measResult.clear();
    m_measOk = true;
    control(1, 0, 0, 0);   // correction off while measuring
    qCInfo(lcAnanPs).nospace() << "PureSignal delay measurement: start (correction off), amp delay now "
                               << m_s.ampDelayNs << " ns, feedback " << m_info[4] << ", att " << m_s.attDb << " dB";
    emit statusChanged();
    return true;
}

void AnanPureSignal::measFinish(const QString& result, bool ok)
{
    m_measStep = -1;
    m_measResult = result;
    m_measOk = ok;
    // Back to the mode: Auto re-learns with the (new) delay, Single makes a
    // new calibration (the reset dropped the old correction).
    if (m_s.enabled) {
        if (m_s.mode == Mode::Auto) {
            control(0, 0, 1, 0);
        } else {
            control(1, 1, 0, 0);
            m_singleRunning = true;
        }
    }
    qCInfo(lcAnanPs).noquote() << "PureSignal delay measurement:" << result;
    emit statusChanged();
}

void AnanPureSignal::measTick()
{
    if (m_measStep < 0)
        return;
    if (!m_s.enabled || !m_keyed || m_cw || !m_feedOk.load(std::memory_order_relaxed)) {
        measFinish(tr("aborted (transmission stopped), amp delay kept at %1 ns").arg(m_s.ampDelayNs, 0, 'f', 0), false);
        return;
    }
    if (m_band != m_measBand) {
        measFinish(tr("aborted (band changed)"), false);
        return;
    }
    const long long t = nowMs() - m_measStartMs;
    if (t > kMeasTimeoutMs) {
        measFinish(tr("no steady reading in %1 s (envelope correlation below 0.95), amp delay kept")
                       .arg(kMeasTimeoutMs / 1000), false);
        return;
    }
    if (t < kMeasSettleMs || m_info[14] != 0 || m_delaySeq == m_measSeqSeen)
        return;   // correction still applied, or no new reading
    m_measSeqSeen = m_delaySeq;
    if (m_delayCorr < 0.95 || std::isnan(m_compDb))
        return;
    m_measDelays.push_back(m_delayNs);
    m_measComps.push_back(m_compDb);
    m_measFitDelays.push_back(m_fitDelayNs);
    m_measFitRes.push_back(m_fitResDb);
    m_measFitResOld.push_back(m_fitResOldDb);
    m_measStep = int(m_measDelays.size());
    emit statusChanged();
    if (m_measStep < kMeasSteps)
        return;
    auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    bool fitOk = true;
    for (std::size_t k = 0; k < m_measFitDelays.size(); ++k)
        fitOk = fitOk && std::isfinite(m_measFitDelays[k]) && std::isfinite(m_measFitRes[k]) &&
                std::isfinite(m_measFitResOld[k]);
    // 02.11: the delay is what calcc needs -- the alignment at which ONE
    // curve (gain and phase over the amplitude) explains the output; the
    // envelope correlation only times the envelope and its peak is broad
    // (the two-tone beat), so it moved by hundreds of ns between measurements
    // (600 -> 1210 -> 1580 ns). The memoryless fit decides when it worked.
    const std::vector<double>& use = fitOk ? m_measFitDelays : m_measDelays;
    const char* how = fitOk ? "memoryless fit" : "envelope correlation (the fit found no minimum)";
    const double d = median(use);
    const double c = median(m_measComps);
    const auto mm = std::minmax_element(use.begin(), use.end());
    const double spread = *mm.second - *mm.first;
    QString vals;
    for (std::size_t k = 0; k < m_measDelays.size(); ++k)
        vals += QStringLiteral(" %1/%2/%3/%4..%5")
                    .arg(m_measDelays[k], 0, 'f', 0)
                    .arg(m_measFitDelays[k], 0, 'f', 0)
                    .arg(m_measComps[k], 0, 'f', 2)
                    .arg(m_measFitRes[k], 0, 'f', 1)
                    .arg(m_measFitResOld[k], 0, 'f', 1);
    qCInfo(lcAnanPs).noquote() << "PureSignal delay measurement: readings (envelope ns / fit ns / "
                                  "compression dB / fit residual dB at the fit delay .. at the current"
                               << m_s.ampDelayNs << "ns):" << vals << "-- using the" << how;
    if (spread > kMeasMaxSpreadNs) {
        measFinish(tr("readings spread %1 ns, not steady -- amp delay kept at %2 ns")
                       .arg(spread, 0, 'f', 0).arg(m_s.ampDelayNs, 0, 'f', 0), false);
        return;
    }
    if (c < -kMeasMaxCompDb) {
        measFinish(tr("amplifier compresses %1 dB at the peak (limit %2 dB) -- lower the power "
                      "and measure again; amp delay kept at %3 ns")
                       .arg(-c, 0, 'f', 1).arg(kMeasMaxCompDb, 0, 'f', 1).arg(m_s.ampDelayNs, 0, 'f', 0), false);
        return;
    }
    const double newNs = 10.0 * std::round(d / 10.0);
    const double old = m_s.ampDelayNs;
    m_s.ampDelayNs = newNs;
    m_delayMeasured = true;
    m_delayMeasuredAt = QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"));
    m_delayMeasuredCompDb = c;
    auto& st = AppSettings::instance();
    const QString k = bandKey(m_band.isEmpty() ? QStringLiteral("unknown") : m_band);
    st.setValue(delayKey("AnanPsDelayNs_", k), m_s.ampDelayNs);
    st.setValue(delayKey("AnanPsDelayAt_", k), m_delayMeasuredAt);
    st.setValue(delayKey("AnanPsDelayCompDb_", k), c);
    m_heatAtMeas = m_heat;
    st.setValue(delayKey("AnanPsDelayHeat_", k), QString::number(m_heatAtMeas, 'f', 3));
    save();
    applyWdspParams();
    measFinish(tr("%1: amp delay %2 -> %3 ns (%4, spread %5 ns, fit residual %6 -> %7 dB, peak gain %8 dB)")
                   .arg(m_band).arg(old, 0, 'f', 0).arg(m_s.ampDelayNs, 0, 'f', 0)
                   .arg(fitOk ? tr("fit") : tr("envelope"))
                   .arg(spread, 0, 'f', 0)
                   .arg(fitOk ? median(m_measFitResOld) : NAN, 0, 'f', 1)
                   .arg(fitOk ? median(m_measFitRes) : NAN, 0, 'f', 1)
                   .arg(c, 0, 'f', 2),
               true);
}

// The memoryless fit (02.11): for a trial delay d (the feedback lags the
// reference by d), rx[i] ~ G(|x|) * x(t_i - d) with x the reference
// interpolated (cubic; the two-tone and its products are a few kHz wide at
// 192 kHz, so the interpolation is exact to far below the noise), G complex
// and linear in |x| inside each of 16 amplitude bins -- the AM/AM and AM/PM
// curves calcc fits. The residual |rx - G x|^2 / |rx|^2 is lowest where the
// rising and falling envelope give the same curve: that IS the alignment
// calcc needs. Scanned around the envelope estimate, refined by a parabola.
void AnanPureSignal::fitDelay(const std::vector<double>& tx, const std::vector<double>& rx)
{
    m_fitDelayNs = m_fitResDb = m_fitResOldDb = NAN;
    const int n = int(std::min(tx.size(), rx.size()) / 2);
    if (n < 2048)
        return;
    double xmax = 0.0;
    for (int i = 0; i < n; ++i)
        xmax = std::max(xmax, std::hypot(tx[std::size_t(2 * i)], tx[std::size_t(2 * i + 1)]));
    if (!(xmax > 0.0))
        return;
    constexpr int kBins = 16;
    constexpr int kEdge = 16;
    std::vector<std::complex<double>> xs(static_cast<std::size_t>(n));
    std::vector<int> bin(static_cast<std::size_t>(n));
    auto X = [&](int k) { return std::complex<double>(tx[std::size_t(2 * k)], tx[std::size_t(2 * k + 1)]); };
    auto residualDb = [&](double dNs) -> double {
        const double dS = dNs * 1.0e-9 * kFeedbackRateHz;
        std::array<double, kBins> s11{}, s12{}, s22{};
        std::array<std::complex<double>, kBins> y1{}, y2{};
        for (int i = kEdge; i < n - kEdge; ++i) {
            const double t = double(i) - dS;
            const int k = int(std::floor(t));
            const double f = t - k;
            if (k < 1 || k + 2 >= n) {
                bin[std::size_t(i)] = -1;
                continue;
            }
            const auto p0 = X(k - 1), p1 = X(k), p2 = X(k + 1), p3 = X(k + 2);
            const auto x = p1 + 0.5 * f * (p2 - p0 + f * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + f * (3.0 * (p1 - p2) + p3 - p0)));
            xs[std::size_t(i)] = x;
            const double u = std::abs(x) / xmax;
            if (u < 0.05) {
                bin[std::size_t(i)] = -1;
                continue;
            }
            const int b = std::min(kBins - 1, int(u * kBins));
            bin[std::size_t(i)] = b;
            const std::complex<double> r(rx[std::size_t(2 * i)], rx[std::size_t(2 * i + 1)]);
            const double w = std::norm(x);
            const std::complex<double> rc = r * std::conj(x);
            s11[std::size_t(b)] += w;
            s12[std::size_t(b)] += w * u;
            s22[std::size_t(b)] += w * u * u;
            y1[std::size_t(b)] += rc;
            y2[std::size_t(b)] += rc * u;
        }
        std::array<std::complex<double>, kBins> ga{}, gb{};
        for (int b = 0; b < kBins; ++b) {
            const double det = s11[std::size_t(b)] * s22[std::size_t(b)] - s12[std::size_t(b)] * s12[std::size_t(b)];
            if (det > 1e-12 * std::max(1e-30, s11[std::size_t(b)] * s22[std::size_t(b)])) {
                ga[std::size_t(b)] = (y1[std::size_t(b)] * s22[std::size_t(b)] - y2[std::size_t(b)] * s12[std::size_t(b)]) / det;
                gb[std::size_t(b)] = (y2[std::size_t(b)] * s11[std::size_t(b)] - y1[std::size_t(b)] * s12[std::size_t(b)]) / det;
            } else if (s11[std::size_t(b)] > 0.0) {
                ga[std::size_t(b)] = y1[std::size_t(b)] / s11[std::size_t(b)];
            }
        }
        double err = 0.0, prx = 0.0;
        for (int i = kEdge; i < n - kEdge; ++i) {
            const int b = bin[std::size_t(i)];
            if (b < 0)
                continue;
            const std::complex<double> r(rx[std::size_t(2 * i)], rx[std::size_t(2 * i + 1)]);
            const auto x = xs[std::size_t(i)];
            const double u = std::abs(x) / xmax;
            err += std::norm(r - (ga[std::size_t(b)] + gb[std::size_t(b)] * u) * x);
            prx += std::norm(r);
        }
        if (!(prx > 0.0))
            return NAN;
        return 10.0 * std::log10(std::max(err / prx, 1e-20));
    };
    constexpr double kSpanNs = 2500.0, kStepNs = 100.0;
    const double c0 = 100.0 * std::round(m_delayNs / 100.0);
    std::vector<double> ds, es;
    for (double d = c0 - kSpanNs; d <= c0 + kSpanNs + 1.0; d += kStepNs) {
        ds.push_back(d);
        es.push_back(residualDb(d));
    }
    int best = -1;
    for (int k = 0; k < int(es.size()); ++k)
        if (std::isfinite(es[std::size_t(k)]) && (best < 0 || es[std::size_t(k)] < es[std::size_t(best)]))
            best = k;
    m_fitResOldDb = residualDb(effectiveDelayNs());
    if (best <= 0 || best >= int(es.size()) - 1)
        return;   // no minimum inside the scan
    const double y0 = es[std::size_t(best - 1)], y1 = es[std::size_t(best)], y2 = es[std::size_t(best + 1)];
    const double den = y0 - 2.0 * y1 + y2;
    const double frac = den > 0.0 ? std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0;
    m_fitDelayNs = ds[std::size_t(best)] + frac * kStepNs;
    m_fitResDb = residualDb(m_fitDelayNs);
}

void AnanPureSignal::setConjugate(bool on)
{
    if (on == m_s.conjugate)
        return;
    m_s.conjugate = on;
    m_conjugate.store(on);
    save();
    qCInfo(lcAnanPs) << "PureSignal: feedback pairs" << (on ? "conjugated (diagnostic)" : "as received");
    resetCorrection();
    emit settingsChanged();
}

QString AnanPureSignal::scheckText(int v)
{
    if (v == 0)
        return {};
#if AETHER_WDSP_PS_CLASSIC
    QStringList t;
    if (v & 0x01) t << QStringLiteral("NaN in the curves");
    if (v & 0x02) t << QStringLiteral("empty segment");
    if (v & 0x04) t << QStringLiteral("corrected drive > 1");
    if (v & 0x08) t << QStringLiteral("gain at full scale > 1.07");
    if (v & 0x10) t << QStringLiteral("corrected drive < 0");
    if (v & 0x20) t << QStringLiteral("gain at full scale < 0");
    if (v & 0x40) t << QStringLiteral("gain moved > 0.05 since the last calibration");
    return t.join(QStringLiteral(", "));
#else
    return QStringLiteral("0x%1").arg(v, 0, 16);
#endif
}

QString AnanPureSignal::calcFailText(const std::array<int, 16>& in)
{
    QStringList t;
#if AETHER_WDSP_PS_CLASSIC
    for (int k = 0; k < 4; ++k)
        if (in[std::size_t(k)])
            t << QStringLiteral("info%1 0x%2").arg(k).arg(in[std::size_t(k)], 0, 16);
    if (in[6])
        t << scheckText(in[6]);
#else
    // WDSP 2.10 calcc: info0 the rx scale, info1/2/3 the AM/AM, AM/PM cos,
    // AM/PM sin fits (calc(), binfo bits), info6 scheck / the top-bucket
    // "deadlock" reset (LCOLLECT / LCALC).
    if (in[0] & 0x01)
        t << QStringLiteral("output level at full drive not found (extrapolation unsure)");
    static const char* kCurve[] = {"", "AM/AM", "AM/PM cos", "AM/PM sin"};
    for (int k = 1; k <= 3; ++k) {
        const int v = in[std::size_t(k)];
        if (!v)
            continue;
        QStringList b;
        if (v & 0x01) b << QStringLiteral("no fit");
        if (v & 0x02) b << QStringLiteral("fit quality bad");
        if (v & 0x04) b << QStringLiteral("spline not built");
        if (v & 0x08) b << QStringLiteral("spline error > 50x noise");
        if (v & 0x10) b << (k == 1 ? QStringLiteral("more than 2 extrema") : QStringLiteral("sin^2+cos^2 != 1"));
        if (v & 0x20) b << QStringLiteral("cold start: low end too high");
        if (v & ~0x3f) b << QStringLiteral("0x%1").arg(v & ~0x3f, 0, 16);
        t << QStringLiteral("%1: %2").arg(QLatin1String(kCurve[k]), b.join(QStringLiteral("/")));
    }
    if (in[6] & 0x01)
        t << QStringLiteral("scheck (curve out of limits)");
    if (in[6] & 0x02)
        t << QStringLiteral("top bucket unreachable (deadlock reset)");
    if (in[6] & ~0x03)
        t << QStringLiteral("info6 0x%1").arg(in[6] & ~0x03, 0, 16);
#endif
    return t.join(QStringLiteral("; "));
}

void AnanPureSignal::setAutoHwPeak(bool on)
{
    m_s.autoHwPeak = on;
    save();
}

void AnanPureSignal::applySettings(const Settings& in)
{
    const bool wasEnabled = m_s.enabled;
    const Mode oldMode = m_s.mode;
    Settings s = in;
    s.enabled = wasEnabled;   // on/off goes through setEnabled()
    s.mode = oldMode;
    forceConstants(s);
    s.hwPeak = m_s.hwPeak;          // measured, not a profile setting
    s.ampDelayNs = m_s.ampDelayNs;
    s.attDbByAdc[std::size_t(std::clamp(s.feedbackAdc, 0, 1))] = s.attDb;
    const bool adcChanged = s.feedbackAdc != m_s.feedbackAdc;
    m_s = s;
    save();
    if (adcChanged && !m_band.isEmpty())
        loadBandDelay();   // that ADC's own delay memory (03.11)
    applyWdspParams();
    pushTransport();
    if (in.mode != oldMode)
        setMode(in.mode);
    if (in.enabled != wasEnabled)
        setEnabled(in.enabled);
    emit statusChanged();
    emit settingsChanged();
}

void AnanPureSignal::resetCorrection()
{
    if (!m_s.enabled) {
        control(1, 0, 0, 0);
        return;
    }
    // Thetis: Reset drops the correction. Auto starts learning again; Single
    // stays uncorrected until Calibrate (it used to start a new calibration
    // at once, so Reset looked exactly like Calibrate).
    m_singleRunning = false;
    control(1, 0, m_s.mode == Mode::Auto ? 1 : 0, 0);
}

void AnanPureSignal::setFeedbackSource(int source)
{
    source = std::clamp(source, 0, 2);
    if (source == m_s.feedbackSource)
        return;
    m_s.feedbackSource = source;
    save();
    pushTransport();
}

void AnanPureSignal::setTransport(QObject* transport)
{
    m_transport = transport;
    m_p2 = qobject_cast<P2Transport*>(transport);
}

void AnanPureSignal::setFeedbackAdc(int adc)
{
    adc = std::clamp(adc, 0, 1);
    if (adc == m_s.feedbackAdc)
        return;
    m_s.attDbByAdc[std::size_t(m_s.feedbackAdc)] = m_s.attDb;
    if (m_measStep >= 0)
        measFinish(tr("aborted (feedback ADC changed)"), false);
    m_s.feedbackAdc = adc;
    m_s.attDb = m_s.attDbByAdc[std::size_t(adc)];   // that ADC's own attenuator value
    m_aa = AaState::Monitor;
    qCInfo(lcAnanPs) << "PureSignal: feedback ADC" << adc + 1 << ", attenuator" << m_s.attDb << "dB";
    save();
    pushTransport();
    // The other path's own delay for this band (its own memory, 03.11).
    loadBandDelay();
    // Another feedback path: the learnt correction does not belong to it.
    if (m_s.enabled) {
        control(1, m_s.mode == Mode::Single ? 1 : 0, m_s.mode == Mode::Auto ? 1 : 0, 0);
        if (m_s.mode == Mode::Single)
            m_singleRunning = true;
    }
    emit statusChanged();
    emit settingsChanged();
}

// The band's delay memory (02.11) belongs to one feedback path: ADC2 (RX2 in,
// an external coupler / attenuator) has its own cables and its own delay
// (operator, 03.11). ADC1 keeps the original keys (AnanPsDelayNs_<band>);
// ADC2 gets AnanPsDelayNsAdc2_<band> and so on.
QString AnanPureSignal::delayKey(const char* base, const QString& bandK) const
{
    QString b = QString::fromLatin1(base);   // "AnanPsDelayNs_"
    if (m_s.feedbackAdc == 1)
        b.insert(b.size() - 1, QStringLiteral("Adc2"));   // "AnanPsDelayNsAdc2_"
    return b + bandK;
}

void AnanPureSignal::setTxFrequency(double hz)
{
    if (hz <= 0.0)
        return;
    const QString band = bandFor(hz);
    if (band == m_band)
        return;
    m_band = band;
    loadBandDelay();
}

void AnanPureSignal::loadBandDelay()
{
    auto& st = AppSettings::instance();
    const QString k = bandKey(m_band);
    bool ok = false;
    const double v = st.value(delayKey("AnanPsDelayNs_", k), QString()).toString().toDouble(&ok);
    const double old = m_s.ampDelayNs;
    m_delayMeasured = ok && std::abs(v) < 20000.0;
    m_s.ampDelayNs = m_delayMeasured ? v : kAmpDelayNs;
    m_delayMeasuredAt = m_delayMeasured ? st.value(delayKey("AnanPsDelayAt_", k), QString()).toString()
                                        : QString();
    bool okC = false;
    const double c = st.value(delayKey("AnanPsDelayCompDb_", k), QString()).toString().toDouble(&okC);
    m_delayMeasuredCompDb = (m_delayMeasured && okC) ? c : NAN;
    bool okH = false;
    const double h = st.value(delayKey("AnanPsDelayHeat_", k), QString()).toString().toDouble(&okH);
    m_heatAtMeas = (m_delayMeasured && okH) ? std::clamp(h, 0.0, 4.0) : 0.0;
    m_measResult.clear();
    qCInfo(lcAnanPs).nospace() << "PureSignal: band " << m_band << ", feedback ADC" << m_s.feedbackAdc + 1
                               << ", amp delay " << m_s.ampDelayNs << " ns ("
                               << (m_delayMeasured ? "measured " + m_delayMeasuredAt : QStringLiteral("default"))
                               << ")";
    if (m_s.ampDelayNs != old || !std::isfinite(m_delayAppliedNs))
        applyWdspParams();
    emit statusChanged();
}

double AnanPureSignal::effectiveDelayNs() const
{
    return m_s.ampDelayNs + kHeatDelayNs * (m_heat - m_heatAtMeas);
}

// The heat state, every tick (50 ms); the WDSP delay follows it in 10 ns
// steps, once a 10 s log line while transmitting.
void AnanPureSignal::heatTick()
{
    const long long now = nowMs();
    if (m_heatLastMs == 0) {
        m_heatLastMs = now;
        return;
    }
    const double dt = std::clamp(double(now - m_heatLastMs) / 1000.0, 0.0, 5.0);
    m_heatLastMs = now;
    double u = 0.0;
    if (m_keyed && !m_cw && m_p2) {
        const double r = double(m_p2->driveLevel()) / kHeatRefDrive;
        u = r * r;
    }
    const double tau = u > m_heat ? kHeatTauS : kCoolTauS;
    m_heat += (u - m_heat) * (1.0 - std::exp(-dt / tau));
    if (m_channel < 0 || m_measStep >= 0)
        return;
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (!txaHold)
        return;   // the channel is being rebuilt; setTxChannel() re-applies the delay
    const double eff = effectiveDelayNs();
    if (!std::isfinite(m_delayAppliedNs) || std::abs(eff - m_delayAppliedNs) >= kHeatApplyStepNs) {
        m_delayAppliedNs = eff;
        SetPSTXDelay(m_channel, 1.0e-9 * eff);
    }
    if (m_keyed && now - m_heatLogMs >= 10000) {
        m_heatLogMs = now;
        qCInfo(lcAnanPs).nospace() << "PureSignal heat delay: heat " << QString::number(m_heat, 'f', 2)
                                   << " (measured at " << QString::number(m_heatAtMeas, 'f', 2) << "), amp delay "
                                   << m_s.ampDelayNs << " + " << QString::number(eff - m_s.ampDelayNs, 'f', 0)
                                   << " = " << QString::number(eff, 'f', 0) << " ns, drive "
                                   << (m_p2 ? m_p2->driveLevel() : -1);
    }
}

void AnanPureSignal::setAutoAttenuate(bool on)
{
    m_s.autoAttenuate = on;
    save();
    pushTransport();
}

void AnanPureSignal::setAttenuation(int db)
{
    db = std::clamp(db, 0, 31);
    if (db == m_s.attDb)
        return;
    m_s.attDb = db;
    m_s.attDbByAdc[std::size_t(m_s.feedbackAdc)] = db;
    save();
    pushTransport();
    emit statusChanged();
}

void AnanPureSignal::setParams(double hwPeak, double moxDelaySec, double ampDelayNs)
{
    m_s.hwPeak = std::clamp(hwPeak, 0.01, 2.0);
    m_s.moxDelaySec = std::clamp(moxDelaySec, 0.0, 10.0);
    m_s.ampDelayNs = std::clamp(ampDelayNs, -1000000.0, 1000000.0);   // WDSP delay: +-1024 samples
    save();
    applyWdspParams();
}

void AnanPureSignal::setTwoToneParams(double f1, double f2, double mag)
{
    m_s.ttFreq1 = std::clamp(f1, 50.0, 5000.0);
    m_s.ttFreq2 = std::clamp(f2, 50.0, 5000.0);
    m_s.ttMag = std::clamp(mag, 0.01, 0.5);
    save();
    if (m_twoTone)
        setTwoTone(true);
}

void AnanPureSignal::setTwoTone(bool on)
{
    m_twoTone = on;
    if (m_txDsp) {
        QMetaObject::invokeMethod(m_txDsp, "setTwoTone", Qt::QueuedConnection,
                                  Q_ARG(bool, on), Q_ARG(double, m_s.ttFreq1),
                                  Q_ARG(double, m_s.ttFreq2), Q_ARG(double, m_s.ttMag));
    }
    emit statusChanged();
}

bool AnanPureSignal::saveCorrection(const QString& file)
{
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (!txaHold)
        return false;
    if (m_channel < 0)
        return false;
    QByteArray name = file.toLocal8Bit();
    PSSaveCorr(m_channel, name.data());
    return true;
}

bool AnanPureSignal::restoreCorrection(const QString& file)
{
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (!txaHold)
        return false;
    if (m_channel < 0 || !m_s.enabled)
        return false;
    QByteArray name = file.toLocal8Bit();
    PSRestoreCorr(m_channel, name.data());
    control(0, 0, 0, 1);   // Thetis IntiateRestoredCorrection: turn on
    if (m_s.mode == Mode::Auto) {
        m_s.mode = Mode::Single;   // a restored correction is kept, not re-learnt
        save();
    }
    return true;
}

AnanPureSignal::Status AnanPureSignal::status() const
{
    Status s;
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (!txaHold)
        return s;
    s.available = m_channel >= 0;
    s.enabled = m_s.enabled;
    s.twoTone = m_twoTone;
    s.attDb = m_s.attDb;
    s.calibrating = m_singleRunning;
    s.delayNs = m_delayNs;
    s.delayCorr = m_delayCorr;
    s.imd3Host = m_imd3Host;
    s.imd3Ref = m_imd3Ref;
    s.imd3Fb = m_imd3Fb;
    s.compressionDb = m_compDb;
    s.delayMeasured = m_delayMeasured;
    s.band = m_band;
    s.delayMeasuredAt = m_delayMeasuredAt;
    s.delayMeasuredCompDb = m_delayMeasuredCompDb;
    s.measStep = m_measStep;
    s.measSteps = kMeasSteps;
    s.measResult = m_measResult;
    s.measOk = m_measOk;
    s.calcFails = m_calcFails;
    s.fitDelayNs = m_fitDelayNs;
    s.heat = m_heat;
    s.effDelayNs = effectiveDelayNs();
    s.heatDriftNs = s.effDelayNs - m_s.ampDelayNs;
    s.fitResDb = m_fitResDb;
    s.calcDrops = m_calcDrops;
    s.lastCalcFail = m_lastCalcFail;
    if (nowMs() - m_hwMeasuredMs.load(std::memory_order_relaxed) < 5000)
        s.hwMeasured = m_hwMeasured.load(std::memory_order_relaxed);
    s.info = m_info;
    s.feedbackLevel = m_info[4];
    s.calibrations = m_info[5];
    s.correcting = m_info[14] == 1;
    s.state = m_info[15];
    s.pairs = m_pairs.load(std::memory_order_relaxed);
    s.feeding = m_feedOk.load(std::memory_order_relaxed)
                && nowMs() - m_lastFeedMs.load(std::memory_order_relaxed) < 300;
    if (m_channel >= 0) {
        double v = 0.0;
        GetPSMaxTX(m_channel, &v);
        s.maxTx = v;
        GetPSHWPeak(m_channel, &v);
        s.hwPeak = v;
    }
    return s;
}

bool AnanPureSignal::feedbackSnapshot(std::vector<double>& rxIq) const
{
    std::lock_guard<std::mutex> lock(m_snapMutex);
    if (m_snapFill < kSnapPairs)
        return false;
    rxIq.resize(std::size_t(2 * kSnapPairs));
    for (int i = 0; i < kSnapPairs; ++i) {
        const int k = (m_snapPos + i) % kSnapPairs;
        rxIq[std::size_t(2 * i)] = m_snap[std::size_t(2 * k)];
        rxIq[std::size_t(2 * i + 1)] = m_snap[std::size_t(2 * k + 1)];
    }
    return true;
}

bool AnanPureSignal::pairSnapshot(std::vector<double>& txIq, std::vector<double>& rxIq) const
{
    std::lock_guard<std::mutex> lock(m_snapMutex);
    if (m_snapFill < kSnapPairs)
        return false;
    txIq.resize(std::size_t(2 * kSnapPairs));
    rxIq.resize(std::size_t(2 * kSnapPairs));
    for (int i = 0; i < kSnapPairs; ++i) {
        const int k = (m_snapPos + i) % kSnapPairs;
        txIq[std::size_t(2 * i)] = m_snapTx[std::size_t(2 * k)];
        txIq[std::size_t(2 * i + 1)] = m_snapTx[std::size_t(2 * k + 1)];
        rxIq[std::size_t(2 * i)] = m_snap[std::size_t(2 * k)];
        rxIq[std::size_t(2 * i + 1)] = m_snap[std::size_t(2 * k + 1)];
    }
    return true;
}

// ANAN-100D port (operator, 01.11: "Calibrate and Reset give the same -30,
// the correction curve does not move"): the calibration pairs every DDC1
// sample with the DDC0 sample of the same instant. If the two streams are
// shifted, AM/AM and AM/PM open into loops (rising and falling envelope
// differ) and a memoryless correction cannot fit them -- on the simulator 4
// samples cost 20 dB, 8 samples 25 dB. Measured here from the data: the
// cross-correlation of the two envelopes (means removed) over +-79 samples
// (less than half the two-tone beat period at 1200 Hz spacing, 160
// samples), peak refined by a parabola. Positive = the feedback lags the
// reference; the Amp delay that aligns them is this value (SetPSTXDelay
// delays the reference, a negative value delays the feedback).
void AnanPureSignal::measureDelay()
{
    std::vector<double> tx, rx;
    if (!pairSnapshot(tx, rx))
        return;
    const int n = kSnapPairs;
    std::vector<double> et(static_cast<std::size_t>(n)), er(static_cast<std::size_t>(n));
    double mt = 0.0, mr = 0.0;
    for (int i = 0; i < n; ++i) {
        et[std::size_t(i)] = std::hypot(tx[std::size_t(2 * i)], tx[std::size_t(2 * i + 1)]);
        er[std::size_t(i)] = std::hypot(rx[std::size_t(2 * i)], rx[std::size_t(2 * i + 1)]);
        mt += et[std::size_t(i)];
        mr += er[std::size_t(i)];
    }
    mt /= n;
    mr /= n;
    if (mt < 1e-6 || mr < 1e-6)
        return;
    double st = 0.0, sr = 0.0;
    for (int i = 0; i < n; ++i) {
        et[std::size_t(i)] -= mt;
        er[std::size_t(i)] -= mr;
        st += et[std::size_t(i)] * et[std::size_t(i)];
        sr += er[std::size_t(i)] * er[std::size_t(i)];
    }
    if (st <= 0.0 || sr <= 0.0)
        return;
    constexpr int kMaxLag = 79;
    std::array<double, 2 * kMaxLag + 1> c{};
    for (int lag = -kMaxLag; lag <= kMaxLag; ++lag) {
        double acc = 0.0;
        // rx[i] against tx[i - lag]
        const int i0 = std::max(kMaxLag, lag), i1 = std::min(n - kMaxLag, n + lag);
        for (int i = i0; i < i1; ++i)
            acc += er[std::size_t(i)] * et[std::size_t(i - lag)];
        c[std::size_t(lag + kMaxLag)] = acc / std::sqrt(st * sr);
    }
    int best = 0;
    for (int k = 1; k < int(c.size()); ++k)
        if (c[std::size_t(k)] > c[std::size_t(best)])
            best = k;
    double frac = 0.0;
    if (best > 0 && best < int(c.size()) - 1) {
        const double y0 = c[std::size_t(best - 1)], y1 = c[std::size_t(best)], y2 = c[std::size_t(best + 1)];
        const double den = y0 - 2.0 * y1 + y2;
        if (den < 0.0)
            frac = std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5);
    }
    m_delayNs = (best - kMaxLag + frac) * 1.0e9 / kFeedbackRateHz;
    m_delayCorr = c[std::size_t(best)];
    // The fit only matters for the measurement (correction off); taken
    // every second anyway and shown in the Amp View.
    if (!m_loopback)
        fitDelay(tx, rx);
    // Linearity: the feedback / reference gain at the top of the envelope
    // (0.85..1 of the peak) against 0.25..0.5, aligned at the whole lag.
    {
        const int L = best - kMaxLag;
        double peak = 0.0;
        for (int i = kMaxLag; i < n - kMaxLag; ++i)
            peak = std::max(peak, et[std::size_t(i - L)] + mt);
        double sLoT = 0.0, sLoR = 0.0, sHiT = 0.0, sHiR = 0.0;
        for (int i = kMaxLag; i < n - kMaxLag && peak > 0.0; ++i) {
            const double a = et[std::size_t(i - L)] + mt, r = er[std::size_t(i)] + mr;
            const double u = a / peak;
            if (u >= 0.25 && u <= 0.5) {
                sLoT += a;
                sLoR += r;
            } else if (u >= 0.85) {
                sHiT += a;
                sHiR += r;
            }
        }
        m_compDb = (sLoT > 0.0 && sHiT > 0.0 && sLoR > 0.0 && sHiR > 0.0)
                       ? 20.0 * std::log10((sHiR / sHiT) / (sLoR / sLoT))
                       : NAN;
    }
    ++m_delaySeq;
    if (m_loopback) {
        // Both DDCs on the DAC: they must be the same signal. For each whole
        // lag L in -4..4, the best complex scale a (rx[i] ~ a * tx[i - L]) and
        // the residual relative to rx -- at the true lag it is the floor of
        // the 24-bit DDCs (~ -100 dB), one sample off it is tens of dB worse.
        QString t;
        int bestL = 0;
        double bestE = 1e9;
        for (int L = -4; L <= 4; ++L) {
            std::complex<double> num = 0.0;
            double den = 0.0, prx = 0.0;
            for (int i = 8; i < n - 8; ++i) {
                const std::complex<double> r(rx[std::size_t(2 * i)], rx[std::size_t(2 * i + 1)]);
                const std::complex<double> x(tx[std::size_t(2 * (i - L))], tx[std::size_t(2 * (i - L) + 1)]);
                num += r * std::conj(x);
                den += std::norm(x);
                prx += std::norm(r);
            }
            if (den <= 0.0 || prx <= 0.0)
                return;
            const std::complex<double> a = num / den;
            double err = 0.0;
            for (int i = 8; i < n - 8; ++i) {
                const std::complex<double> r(rx[std::size_t(2 * i)], rx[std::size_t(2 * i + 1)]);
                const std::complex<double> x(tx[std::size_t(2 * (i - L))], tx[std::size_t(2 * (i - L) + 1)]);
                err += std::norm(r - a * x);
            }
            const double e = 10.0 * std::log10(std::max(err / prx, 1e-20));
            t += QStringLiteral(" %1:%2").arg(L).arg(e, 0, 'f', 1);
            if (e < bestE) {
                bestE = e;
                bestL = L;
            }
        }
        qCInfo(lcAnanPs).noquote().nospace()
            << "PureSignal LOOPBACK TEST (DDC0 = DDC1 = DAC, 192 kHz, same NCO): best whole lag "
            << bestL << " samples (+ = DDC0 later than DDC1), residual " << QString::number(bestE, 'f', 1)
            << " dB; envelope lag " << QString::number(m_delayNs, 'f', 0) << " ns ("
            << QString::number(m_delayNs * kFeedbackRateHz * 1e-9, 'f', 2) << " samples); residual by lag [dB]:" << t;
        return;
    }
    if (std::abs(m_delayNs - m_delayLoggedNs) > 1000.0) {
        m_delayLoggedNs = m_delayNs;
        qCInfo(lcAnanPs).nospace() << "PureSignal: TX reference -> feedback delay measured "
                                   << m_delayNs << " ns (" << (m_delayNs * kFeedbackRateHz * 1e-9)
                                   << " samples at 192 kHz, envelope correlation " << m_delayCorr
                                   << "), Amp delay set " << m_s.ampDelayNs << " ns; RMS reference "
                                   << std::sqrt(st / n + mt * mt) << ", feedback " << std::sqrt(sr / n + mr * mr);
    }
}

// Where does the distortion come from? The same IMD3 at the host's DUC IQ
// (before the radio), the DAC data (PS reference: after the FPGA DUC) and
// the coupler (after the amplifier and the feedback path). Logged once a
// second while 2 TONE runs with PureSignal feeding (01.11: -28 dBc at 2 W,
// where the amplifier cannot be the source).
void AnanPureSignal::measureImd()
{
    if (!m_twoTone)
        return;
    // Every 5 s is enough for the log; the Amp View line updates each call.
    static long long lastLogMs = 0;
    const bool logIt = nowMs() - lastLogMs >= 5000;
    if (logIt)
        lastLogMs = nowMs();
    std::vector<double> tx, rx, host;
    if (!pairSnapshot(tx, rx))
        return;
    const auto fb = twoToneImd(rx.data(), std::size_t(kSnapPairs), kFeedbackRateHz, m_s.ttFreq1, m_s.ttFreq2);
    const auto ref = twoToneImd(tx.data(), std::size_t(kSnapPairs), kFeedbackRateHz, m_s.ttFreq1, m_s.ttFreq2);
    TwoToneImd h;
    if (auto* t = qobject_cast<P2Transport*>(m_transport); t && t->ducSnapshot(host))
        h = twoToneImd(host.data(), host.size() / 2, kFeedbackRateHz, m_s.ttFreq1, m_s.ttFreq2);
    m_imd3Host = h.valid ? h.imd3 : NAN;
    m_imd3Ref = ref.valid ? ref.imd3 : NAN;
    // A jump of the output IMD3 between two one-second readings (> 3 dB):
    // logged at once with the calibration it follows.
    if (fb.valid && !std::isnan(m_imd3Fb) && std::abs(fb.imd3 - m_imd3Fb) > 3.0)
        qCInfo(lcAnanPs).nospace() << "PureSignal IMD3 jump: output " << QString::number(m_imd3Fb, 'f', 1)
                                   << " -> " << QString::number(fb.imd3, 'f', 1) << " dBc (DAC "
                                   << QString::number(ref.valid ? ref.imd3 : NAN, 'f', 1) << "), calibration "
                                   << m_info[5] << ", correcting " << m_info[14] << ", feedback " << m_info[4]
                                   << ", att " << m_s.attDb << " dB";
    m_imd3Fb = fb.valid ? fb.imd3 : NAN;
    auto fmt = [](const TwoToneImd& r) {
        return r.valid ? QStringLiteral("IMD3 %1 / IMD5 %2 dBc (tone %3 dBFS, balance %4 dB, tones at %5f, image %6 dB)")
                             .arg(r.imd3, 0, 'f', 1).arg(r.imd5, 0, 'f', 1)
                             .arg(r.toneDbfs, 0, 'f', 1).arg(r.balance, 0, 'f', 2)
                             .arg(r.side > 0 ? QStringLiteral("+") : QStringLiteral("-"))
                             .arg(-r.sideDb, 0, 'f', 1)
                       : QStringLiteral("n/a");
    };
    // The correction (iqc) works in the host IQ's handedness; calcc must see
    // the pairs in the same one, i.e. the tones on the same side.
    if (logIt && h.valid && ref.valid && fb.valid) {
        const bool same = h.side == ref.side && ref.side == fb.side;
        qCInfo(lcAnanPs).noquote()
            << "PureSignal handedness check:" << (same ? "OK" : "MISMATCH")
            << "-- host IQ tones at" << (h.side > 0 ? "+f" : "-f") << ", PS pairs (as fed to calcc,"
            << (m_s.conjugate ? "conjugated" : "raw DDC") << ") at" << (ref.side > 0 ? "+f" : "-f")
            << "/" << (fb.side > 0 ? "+f" : "-f");
    }
    if (logIt && fb.valid) {
        // Where a carrier line comes from: the host's IQ, the DAC data
        // (DDC1, after the FPGA DUC) or only the coupler (DDC0).
        auto cl = [](const double* iq, std::size_t n, const TwoToneImd& r) {
            if (!r.valid)
                return QStringLiteral("n/a");
            const auto c = carrierLine(iq, n, kFeedbackRateHz, std::pow(10.0, r.toneDbfs / 10.0));
            return QStringLiteral("DC %1 dBc, line %2 dBc at %3 Hz")
                .arg(c.dcDbc, 0, 'f', 1).arg(c.peakDbc, 0, 'f', 1).arg(c.peakHz, 0, 'f', 1);
        };
        qCInfo(lcAnanPs).noquote()
            << "PureSignal carrier (DC = the mean; line = strongest within +-150 Hz without it): host DUC IQ"
            << (h.valid ? cl(host.data(), host.size() / 2, h) : QStringLiteral("n/a"))
            << "| DAC data (DDC1)" << cl(tx.data(), std::size_t(kSnapPairs), ref)
            << "| coupler (DDC0)" << cl(rx.data(), std::size_t(kSnapPairs), fb)
            << "| correcting" << m_info[14];
    }
    if (logIt)
        qCInfo(lcAnanPs).noquote() << "PureSignal 2-tone chain: host DUC IQ" << fmt(h)
                               << "| DAC data (DDC1)" << fmt(ref) << "| coupler (DDC0)" << fmt(fb)
                               << "| correcting" << m_info[14];
}

bool AnanPureSignal::display(Disp& out) const
{
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (!txaHold)
        return false;
    if (m_channel < 0)
        return false;
    constexpr int kMax = 8192;   // calcc collects 16 x 256 samples
    constexpr int kPts = 512;    // calcc DISP_PTS
    out.x.assign(kMax, 0.0);
    out.ym.assign(kMax, 0.0);
    out.yc.assign(kMax, 0.0);
    out.ys.assign(kMax, 0.0);
    out.xm.assign(kPts, 0.0);
    out.ymc.assign(kPts, 0.0);
    out.xa.assign(kPts, 0.0);
    out.yac.assign(kPts, 0.0);
    int n = 0, cpts = 0;
    double ref = 0.0;
#if AETHER_WDSP_PS_CLASSIC
    // Classic calcc (Thetis): ints x spi = 16 x 256 samples, and the
    // correction as 16 cubic segments per curve (gain cm, cos cc, sin cs) on
    // x = k / 16 -- evaluated here as iqc.c xiqc() does.
    constexpr int kInts = 16;
    std::array<double, 4 * kInts> cm{}, cc{}, cs{};
    GetPSDisp(m_channel, out.x.data(), out.ym.data(), out.yc.data(), out.ys.data(),
              cm.data(), cc.data(), cs.data());
    n = 0;
    for (int i = 0; i < kInts * 256; ++i)
        if (out.x[std::size_t(i)] != 0.0)
            n = kInts * 256;
    bool haveCorr = false;
    for (double v : cm)
        haveCorr = haveCorr || v != 0.0;
    if (haveCorr) {
        cpts = kPts;
        for (int i = 0; i < kPts; ++i) {
            const double env = double(i) / double(kPts - 1);
            const int k = std::min(int(env * kInts), kInts - 1);
            const double dx = env - double(k) / kInts;
            auto ev = [&](const std::array<double, 4 * kInts>& c) {
                return c[std::size_t(4 * k)] + dx * (c[std::size_t(4 * k + 1)]
                       + dx * (c[std::size_t(4 * k + 2)] + dx * c[std::size_t(4 * k + 3)]));
            };
            out.xm[std::size_t(i)] = env;
            out.ymc[std::size_t(i)] = ev(cm);
            out.xa[std::size_t(i)] = env;
            out.yac[std::size_t(i)] = std::atan2(ev(cs), ev(cc)) * 57.29577951308232;
        }
    }
#else
    GetPSDisp(m_channel, out.x.data(), out.ym.data(), out.yc.data(), out.ys.data(),
              out.xm.data(), out.ymc.data(), out.xa.data(), out.yac.data(), &n, &cpts, &ref);
#endif
    n = std::clamp(n, 0, kMax);
    cpts = std::clamp(cpts, 0, kPts);
    out.x.resize(std::size_t(n));
    out.ym.resize(std::size_t(n));
    out.yc.resize(std::size_t(n));
    out.ys.resize(std::size_t(n));
    out.xm.resize(std::size_t(cpts));
    out.ymc.resize(std::size_t(cpts));
    out.xa.resize(std::size_t(cpts));
    out.yac.resize(std::size_t(cpts));
    out.phsRefDeg = ref;
    return n > 0 || cpts > 0;
}

void AnanPureSignal::tick()
{
    const wdsp::TxaShared txaHold;   // see WdspTxaGuard.h
    if (!txaHold)
        return;
    if (m_channel < 0)
        return;
    heatTick();
    m_oldInfo = m_info;
    GetPSInfo(m_channel, m_info.data());
    const bool changed = m_info != m_oldInfo;
    // calcc health (02.11: "continuous recalibration after re-measuring the
    // delay"; the calibration line only appears for a SUCCESSFUL calcc --
    // binfo[5] counts those -- so failures and calcc's own resets were
    // invisible). A failed calc leaves its reasons in info[0..3]/[6] until
    // the next one; the deadlock reset sets info[6] bit 1 the same way.
    static const char* kStNames[] = {"RESET", "WAIT", "MOXDELAY", "SETUP", "COLLECT",
                                     "MOXCHECK", "CALC", "DELAY", "STAYON", "TURNON"};
    if (m_s.enabled && m_keyed && !m_cw && m_measStep < 0) {
        const bool failNow = m_info[0] || m_info[1] || m_info[2] || m_info[3] || m_info[6];
        const bool failChanged = m_info[0] != m_oldInfo[0] || m_info[1] != m_oldInfo[1] ||
                                 m_info[2] != m_oldInfo[2] || m_info[3] != m_oldInfo[3] ||
                                 m_info[6] != m_oldInfo[6];
        if (failNow && failChanged) {
            ++m_calcFails;
            m_lastCalcFail = calcFailText(m_info);
            qCInfo(lcAnanPs).nospace() << "PureSignal: calcc calibration FAILED (" << m_calcFails
                                       << " this transmission): " << m_lastCalcFail << "; state "
                                       << kStNames[std::clamp(m_info[15], 0, 9)] << ", last good calibration "
                                       << m_info[5] << ", correcting " << m_info[14] << ", feedback "
                                       << m_info[4] << ", att " << m_s.attDb << " dB, amp delay "
                                       << m_s.ampDelayNs << " ns";
        }
#if !AETHER_WDSP_PS_CLASSIC
        // Which point of which curve broke scheck (WDSP patch, every failure
        // -- two in a row leave info[6] unchanged and are counted here).
        {
            double d[6] = {0, 0, 0, 0, 0, 0};
            GetPSScheckDiag(m_channel, d);
            if (d[5] < m_scheckSeen)
                m_scheckSeen = 0.0;   // a new calcc (channel re-opened)
            if (d[5] > m_scheckSeen) {
                static const char* kCurve[] = {"magnitude (m)", "cos (c)", "sin (s)", "magnitude < 0 (m)"};
                qCInfo(lcAnanPs).nospace()
                    << "PureSignal scheck " << (kScheckMode == 1 ? "(off) would have failed" : "FAILED") << " (#"
                    << int(d[5]) << "): worst " << kCurve[std::clamp(int(d[0]), 0, 3)]
                    << " at x " << QString::number(d[2], 'f', 3) << " (point " << int(d[1]) << " of 40): "
                    << QString::number(d[3], 'f', 3) << " -> " << QString::number(d[4], 'f', 3) << " (|d| "
                    << QString::number(std::abs(d[4] - d[3]), 'f', 3) << ", limit 0.100); feedback " << m_info[4]
                    << ", att " << m_s.attDb << " dB, drive "
                    << (m_p2 ? m_p2->driveLevel() : -1);
                m_scheckSeen = d[5];
            }
        }
#endif
        if (m_oldInfo[14] == 1 && m_info[14] == 0 && nowMs() - m_ownResetMs > 300) {
            ++m_calcDrops;
            qCInfo(lcAnanPs).nospace() << "PureSignal: correction DROPPED by calcc (" << m_calcDrops
                                       << " this transmission), state "
                                       << kStNames[std::clamp(m_oldInfo[15], 0, 9)] << " -> "
                                       << kStNames[std::clamp(m_info[15], 0, 9)] << ", reason: "
                                       << (failNow ? calcFailText(m_info)
                                                   : QStringLiteral("none reported (3 failed calibrations in a row, "
                                                                    "or a reset in COLLECT)"))
                                       << "; feedback " << m_info[4] << ", att " << m_s.attDb << " dB";
            emit statusChanged();
        }
    }
    if (m_s.enabled && (m_info[5] != m_lastCalLogged || m_info[15] != m_lastStateLogged)) {
        static const char* kNames[] = {"RESET", "WAIT", "MOXDELAY", "SETUP", "COLLECT",
                                       "MOXCHECK", "CALC", "DELAY", "STAYON", "TURNON"};
        const int st = std::clamp(m_info[15], 0, 9);
        if (m_info[5] != m_lastCalLogged)
            qCInfo(lcAnanPs).nospace() << "PureSignal: calibration " << m_info[5] << ", feedback "
                                       << m_info[4] << ", correcting " << m_info[14] << ", att "
                                       << m_s.attDb << " dB, state " << kNames[st]
                                       << ", info0..3 " << m_info[0] << " " << m_info[1] << " "
                                       << m_info[2] << " " << m_info[3] << ", info6 " << m_info[6]
                                       << ", info12 " << m_info[12]
                                       << (m_info[6] ? QStringLiteral(" (%1)").arg(scheckText(m_info[6])) : QString())
                                       << ", HW peak " << m_s.hwPeak
                                       << ", measured " << m_hwMeasured.load(std::memory_order_relaxed);
        if (m_info[5] != m_lastCalLogged) {
            // What calcc fitted: the amplifier gain ym = tx / rx over the
            // output amplitude x, in five bands -- shows outliers (a wild
            // low-amplitude band makes the spline dip below zero, "corrected
            // drive < 0").
            Disp d;
            if (display(d) && !d.x.empty()) {
                double lo[5], hi[5];
                int cnt[5] = {0, 0, 0, 0, 0};
                for (int b = 0; b < 5; ++b) {
                    lo[b] = 1e9;
                    hi[b] = -1e9;
                }
                for (std::size_t i = 0; i < d.x.size(); ++i) {
                    const double x = d.x[i], y = d.ym[i];
                    if (!(x > 0.0) || !std::isfinite(y))
                        continue;
                    const int b = std::clamp(int(x * 5.0), 0, 4);
                    lo[b] = std::min(lo[b], y);
                    hi[b] = std::max(hi[b], y);
                    ++cnt[b];
                }
                QString t;
                for (int b = 0; b < 5; ++b)
                    t += cnt[b] ? QStringLiteral(" [%1-%2: %3..%4 n%5]")
                                      .arg(b * 0.2, 0, 'f', 1).arg((b + 1) * 0.2, 0, 'f', 1)
                                      .arg(lo[b], 0, 'f', 3).arg(hi[b], 0, 'f', 3).arg(cnt[b])
                                : QStringLiteral(" [%1-%2: none]")
                                      .arg(b * 0.2, 0, 'f', 1).arg((b + 1) * 0.2, 0, 'f', 1);
                // And the amplifier phase per band (deg, relative to the top
                // band; calcc ys/yc as in the Amp View) and the correction's
                // phase range: a jumping yellow curve shows up here.
                double ps[5] = {0, 0, 0, 0, 0};
                int pn[5] = {0, 0, 0, 0, 0};
                for (std::size_t i = 0; i < d.x.size() && i < d.ys.size() && i < d.yc.size(); ++i) {
                    if (!(d.x[i] > 0.0) || !std::isfinite(d.ys[i]) || !std::isfinite(d.yc[i]))
                        continue;
                    const int b = std::clamp(int(d.x[i] * 5.0), 0, 4);
                    ps[b] += -180.0 / 3.14159265358979 * std::atan2(d.ys[i], d.yc[i]);
                    ++pn[b];
                }
                const double top = pn[4] ? ps[4] / pn[4] : 0.0;
                QString ph;
                for (int b = 0; b < 5; ++b)
                    ph += pn[b] ? QStringLiteral(" %1").arg(ps[b] / pn[b] - top, 0, 'f', 1) : QStringLiteral(" --");
                double cpMin = 1e9, cpMax = -1e9;
                for (double v : d.yac) {
                    cpMin = std::min(cpMin, v);
                    cpMax = std::max(cpMax, v);
                }
                qCInfo(lcAnanPs).noquote() << "PureSignal: fitted gain ym by output x:" << t
                                           << "| amp phase by band [deg]:" << ph
                                           << "| correction phase" << QString::number(cpMin, 'f', 1)
                                           << ".." << QString::number(cpMax, 'f', 1);
            }
        }
        m_lastCalLogged = m_info[5];
        m_lastStateLogged = m_info[15];
    }

    // The DDC1 -> DDC0 delay, once a second while the feedback flows.
    if (m_feedOk.load(std::memory_order_relaxed) && nowMs() - m_delayLastMs >= 1000) {
        m_delayLastMs = nowMs();
        measureDelay();
        measureImd();
        // The amp delay is not followed here: it is fixed by
        // startDelayMeasure() (low power, correction off). This reading,
        // taken while correcting, is only shown.
    }

    measTick();

    // A Single calibration ends in STAYON with the correction applied.
    if (m_singleRunning && (!m_s.enabled || (m_info[15] == 8 && m_info[14] == 1)))
        m_singleRunning = false;

    // HW peak: the calibration and the correction only agree when it is the
    // real DDC1 amplitude of a full-scale DUC sample (on a simulated PA a 5%
    // error held IMD3 at -42 dBc instead of -80). P2Transport measures it;
    // follow it when it is steady and differs by more than 1.5%.
    if (m_s.enabled && m_s.autoHwPeak && m_keyed && m_measStep < 0) {
        const long long t = m_hwMeasuredMs.load(std::memory_order_relaxed);
        const double meas = m_hwMeasured.load(std::memory_order_relaxed);
        if (t > m_hwLastUsedMs && meas > 0.01) {
            m_hwLastUsedMs = t;
            if (std::abs(meas / m_s.hwPeak - 1.0) > 0.015) {
                if (++m_hwVotes >= 2) {
                    m_hwVotes = 0;
                    qCInfo(lcAnanPs).nospace() << "PureSignal: HW peak " << m_s.hwPeak
                                               << " -> measured " << meas << ", calibration restarted";
                    m_s.hwPeak = meas;
                    save();
                    applyWdspParams();
                    control(1, m_s.mode == Mode::Single ? 1 : 0, m_s.mode == Mode::Auto ? 1 : 0, 0);
                    if (m_s.mode == Mode::Single)
                        m_singleRunning = true;
                    emit statusChanged();
                }
            } else {
                m_hwVotes = 0;
            }
        }
    }

    // Thetis PSForm.timer2code: keep the feedback level in 128..181 with the
    // feedback ADC's attenuator, re-calibrating after every change.
    const bool attemptsChanged = m_info[5] != m_oldInfo[5];
    if (attemptsChanged && m_aaAttemptsSince < 100)
        ++m_aaAttemptsSince;
    bool fbOverload = false;
    if (auto* t = qobject_cast<P2Transport*>(m_transport)) {
        const unsigned ovl = t->takeOverloadLatch();
        fbOverload = m_keyed && ((ovl >> unsigned(m_s.feedbackAdc)) & 1u) != 0;
    }
    if (m_s.enabled) {
        switch (m_aa) {
        case AaState::Monitor: {
            const int fb = m_info[4];
            // ANAN-100D port (log 22:36): two corrections to Thetis'
            // timer2code. (1) A level is acted on only from the second
            // calibration after an attenuator change and >= 400 ms later --
            // the first still holds samples of the old setting (29 at 31 dB,
            // then the same 29 at 17 dB: stepped to 3 dB, the ADC clipped;
            // 115/184 at 10 dB: hunting 8 <-> 12 dB). (2) An overload of the
            // feedback ADC acts at once (+6 dB): clipped feedback can stall
            // the collection for seconds (8 s at -13 dBc there).
            const bool settled = m_aaAttemptsSince >= 2 && nowMs() - m_aaChangedMs >= 400;
            // ANAN-100D port (operator, 02.11: "let the attenuator follow the
            // level smoothly in 1 dB steps, then no recalibration is needed").
            // calcc normalises the feedback per calibration (rx_scale =
            // 1 / extrapolated rx at full drive, calcc.c calc()), so the
            // absolute level does not enter the correction: a 1 dB step only
            // touches the one collection that spans it. So the normal
            // tracking is 1 dB steps WITHOUT a reset, holding the smoothed
            // level at 152 (+-0.6 dB) -- the slow drift of a warming PA
            // (177 -> 157, 160 -> 128 within a minute) never reaches the
            // Thetis limits. The reset-and-jump of Thetis' timer2code stays
            // for a level far off (> 200, < 100: the first over, a drive
            // change) and for an ADC overload.
            // Drive feed-forward (operator, 04.11: "measure the level and
            // set the attenuator ahead"): P2Transport moves the attenuator
            // together with the drive (setPsAttFollowsDrive); adopt it.
            if (m_p2 && m_s.autoAttenuate) {
                const int now = m_p2->psAttNow();
                if (now != m_s.attDb && now >= 0 && now <= 31 && m_keyed) {
                    qCInfo(lcAnanPs).nospace() << "PureSignal auto-attenuate (drive): drive " << m_p2->driveLevel()
                                               << ", att " << m_s.attDb << " -> " << now
                                               << " dB (set with the drive), correction kept";
                    m_s.attDb = now;
                    m_s.attDbByAdc[std::size_t(m_s.feedbackAdc)] = now;
                    save();
                    emit statusChanged();
                    m_aaChangedMs = nowMs();
                    m_aaAttemptsSince = 0;
                    break;
                }
            }
            const bool needRecal = fb > kFbCoarseHigh || (fb < kFbCoarseLow && m_s.attDb > 0);
            const bool levelOff = attemptsChanged && settled && needRecal;
            if (attemptsChanged && settled && !needRecal && fb > 0)
                m_fbEma = m_fbEma > 0.0 ? m_fbEma + 0.3 * (double(fb) - m_fbEma) : double(fb);
            if (m_s.autoAttenuate && attemptsChanged && settled && !needRecal && !fbOverload
                && m_measStep < 0 && m_fbEma > 0.0
                && (m_fbEma > kFbWinHigh || (m_fbEma < kFbWinLow && m_s.attDb > 0))) {
                const double offDb = 20.0 * std::log10(m_fbEma / kFbTarget);
                const int newAtt = std::clamp(m_s.attDb + int(std::lround(offDb)), 0, 31);
                if (newAtt != m_s.attDb) {
                    qCInfo(lcAnanPs).nospace() << "PureSignal auto-attenuate (window): feedback " << fb << " (smoothed "
                                               << QString::number(m_fbEma, 'f', 0) << ", outside 128..181, "
                                               << QString::number(offDb, 'f', 1) << " dB from 152), att "
                                               << m_s.attDb << " -> " << newAtt << " dB, correction kept";
                    m_fbEma *= std::pow(10.0, -(newAtt - m_s.attDb) / 20.0);   // what the level becomes
                    setAttenuation(newAtt);
                    m_aaChangedMs = nowMs();
                    m_aaAttemptsSince = 0;
                    break;
                }
            }
            const bool clip = fbOverload && nowMs() - m_aaChangedMs >= 400 && m_s.attDb < 31;
            if (m_s.autoAttenuate && (levelOff || clip) && m_measStep < 0) {
                double ddB = 31.1;
                if (fb <= 256) {
                    ddB = 20.0 * std::log10(double(fb) / kFbTarget);
                    if (!std::isfinite(ddB))
                        ddB = -100.0;
                    ddB = std::clamp(ddB, -100.0, 100.0);
                }
                if (clip && !levelOff)
                    ddB = std::max(ddB, 6.0);
                if (clip)
                    qCInfo(lcAnanPs) << "PureSignal auto-attenuate: feedback ADC" << m_s.feedbackAdc + 1
                                     << "overload";
                m_aaDelta = int(std::lround(ddB));
                m_fbEma = 0.0;
                // Thetis timer2code: remember whether Auto was running or a
                // Single calibration was still under way -- restore exactly
                // that (a finished Single correction is not re-learnt).
                m_aaSaveAuto = m_s.mode == Mode::Auto ? 1 : 0;
                m_aaSaveSingle = (m_s.mode == Mode::Single && m_singleRunning) ? 1 : 0;
                control(1, 0, 0, 0);
                m_aa = AaState::SetNewValues;
            }
            break;
        }
        case AaState::SetNewValues: {
            const int newAtt = std::clamp(m_s.attDb + m_aaDelta, 0, 31);
            qCInfo(lcAnanPs) << "PureSignal auto-attenuate: feedback" << m_info[4] << "att"
                             << m_s.attDb << "->" << newAtt << "dB";
            setAttenuation(newAtt);
            m_aaChangedMs = nowMs();
            m_aaAttemptsSince = 0;
            m_aa = AaState::Restore;
            break;
        }
        case AaState::Restore:
            control(0, m_aaSaveSingle, m_aaSaveAuto, 0);
            m_aa = AaState::Monitor;
            break;
        }
    } else {
        m_aa = AaState::Monitor;
    }
    if (changed)
        emit statusChanged();
}

} // namespace AetherSDR::anan
