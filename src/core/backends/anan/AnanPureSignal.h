#pragma once

// ANAN-100D port: PureSignal (adaptive predistortion) for the ANAN-100D over
// openHPSDR Protocol 2 -- the host side Thetis and deskHPSDR implement:
//
//   radio   while MOX with PS on, DDC0 = the coupler feedback (ADC0), DDC1 =
//           the TX DAC, both 192 ksps, synchronised (P2Transport); Alex PS
//           bits; ADC0 attenuator while transmitting = the PS attenuator.
//   WDSP    calcc (calibration, own thread) compares the two and iqc in the
//           TXA chain applies the inverse PA characteristic.
//   here    feeding pscc() on the radio I/O thread, SetPSMox at every key
//           edge, the Thetis command state machine (Auto / Single / Off /
//           restore), automatic attenuation (Thetis PSForm timer2code:
//           feedback level 128..181 is right, 152 the target), 2-TONE via
//           the TXA post-generator, settings in AppSettings (AnanPs*).
//
// Lives on the GUI thread (owned by Hl2Backend); feed() is the only I/O
// thread entry and touches atomics and its own buffers only.

#include <QObject>
#include <QString>

#include <array>
#include <cmath>
#include <atomic>
#include <mutex>
#include <vector>

class QTimer;

namespace AetherSDR::anan {

class AnanPureSignal : public QObject {
    Q_OBJECT
public:
    enum class Mode { Auto = 0, Single = 1 };

    struct Settings {
        bool enabled = false;
        Mode mode = Mode::Auto;
        int feedbackSource = 0;     // 0 internal coupler, 1 EXT1, 2 RX bypass
        int feedbackAdc = 0;        // 0 ADC1, 1 ADC2 (RX2 input)
        bool autoAttenuate = true;
        int attDb = 20;             // feedback ADC attenuator while transmitting, 0..31 (the active one)
        // Remembered per feedback ADC (ADC1, ADC2): the two inputs see very
        // different levels, so one value cannot serve both; attDb is the
        // entry of feedbackAdc.
        std::array<int, 2> attDbByAdc{{20, 20}};
        double hwPeak = 0.2899;     // Protocol 2 TX DAC feedback peak (Thetis)
        bool autoHwPeak = true;     // follow the measured HW peak (DDC1 / DUC x CFIR gain)
        double moxDelaySec = 0.2;
        double ampDelayNs = 150.0;
        double ttFreq1 = 700.0;
        double ttFreq2 = 1900.0;
        double ttMag = 0.49;        // each tone, linear (0.49 + 0.49 ~ full scale)
        // Diagnostic: conjugate the DDC pairs before calcc. OFF is right --
        // the PS DDC pairs already have the host IQ's handedness (measured on
        // a working radio 01.11: tones on the same side, IMD3 -45 dBc; with
        // the conjugate -32, the AM/PM learnt with the wrong sign).
        bool conjugate = false;
    };

    struct Status {
        bool available = false;     // a TXA channel exists
        bool enabled = false;
        bool feeding = false;       // pairs reaching pscc right now
        bool correcting = false;    // info[14]: the correction is applied
        bool twoTone = false;
        int feedbackLevel = 0;      // info[4]
        int calibrations = 0;       // info[5]
        int state = 0;              // info[15], calcc state
        int attDb = 0;
        double maxTx = 0.0;
        double hwPeak = 0.0;
        double hwMeasured = 0.0;    // last measured DDC1 / DUC scale, 0 = none
        bool calibrating = false;   // a Single calibration is under way
        // TX reference (DDC1) -> feedback (DDC0) delay measured from the data
        // (envelope cross-correlation), ns; valid when delayCorr > 0.
        double delayNs = 0.0;
        double delayCorr = 0.0;
        // Two-tone IMD3 (dBc) at three points, NaN when not measured: the
        // host's DUC IQ, the DAC data (PS reference, DDC1), the coupler (DDC0).
        double imd3Host = NAN, imd3Ref = NAN, imd3Fb = NAN;
        // Feedback / reference gain at the top of the envelope (0.85..1 of
        // the peak) against 0.25..0.5, dB: negative = compression, positive =
        // expansion (class AB); meaningful with the correction off. NaN = none.
        double compressionDb = NAN;
        // Odyssey firmware buffer report, session totals (reported = false:
        // older firmware).
        // Delay measurement (low power, correction off): step in progress
        // (-1 = none) of measSteps; whether the amp delay is a measured one,
        // when, at which compression; the last outcome in words.
        int measStep = -1, measSteps = 0;
        bool delayMeasured = false;
        QString band;               // amp delay memory: the transmit band ("40 m"), "" = unknown
        QString delayMeasuredAt;
        double delayMeasuredCompDb = NAN;
        QString measResult;
        bool measOk = true;
        // calcc health (this transmission): calibrations that failed, the
        // corrections calcc itself dropped, and the last reason in words.
        unsigned calcFails = 0, calcDrops = 0;
        // Feedback the calibration was kept away from this session: zero runs
        // in the reference, steps (samples the radio dropped), the radio's
        // TX buffer / dropped-packet reports.
        // The last memoryless fit of the DDC1 -> DDC0 lag and its residual
        // (NaN = none yet); only the measurement uses it.
        double fitDelayNs = NAN, fitResDb = NAN;
        // Heat drift of the delay (anan162): the PA heat state 0..1+, the
        // delay applied now (measured + drift) and the drift part.
        double heat = 0.0, effDelayNs = 0.0, heatDriftNs = 0.0;
        QString lastCalcFail;
        std::array<int, 16> info{};
        unsigned long long pairs = 0;
    };

    explicit AnanPureSignal(QObject* parent = nullptr);
    ~AnanPureSignal() override;

    // Wiring (Hl2Backend). The transport and the TX DSP live on the I/O thread
    // and are reached by queued invokes only.
    void setTransport(QObject* transport);
    void setTxDsp(QObject* txDsp) { m_txDsp = txDsp; }
    // The TXA channel id; re-applies the WDSP parameters when it changes.
    void setTxChannel(int channel);
    // Key edges and the CW state (no PS in CW: the FPGA keyer makes the carrier).
    void setKeyed(bool keyed, bool cw);

    // I/O thread: feedback pairs from P2Transport (interleaved I/Q).
    void feed(const double* tx, const double* rx, int n, double hwPeak = -1.0);

    // Controls (GUI).
    void setEnabled(bool on);
    void setMode(Mode mode);
    void calibrate();         // one Single calibration now
    void resetCorrection();   // drop the correction, restart the current mode
    void setFeedbackSource(int source);
    void setFeedbackAdc(int adc);
    // Transmit frequency (Hl2Backend): selects the band's amp delay.
    void setTxFrequency(double hz);
    void setAutoAttenuate(bool on);
    void setAttenuation(int db);
    void setParams(double hwPeak, double moxDelaySec, double ampDelayNs);
    void setAutoHwPeak(bool on);
    void setConjugate(bool on);
    // Firmware loopback test: DDC0 on the DAC like DDC1 (not saved).
    void setLoopbackTest(bool on);
    // Measure the reference -> feedback delay once, at low power with the
    // correction off, and fix it as the band's amp delay (saved, used
    // unchanged at any power). False if not now.
    bool startDelayMeasure();
    bool loopbackTest() const { return m_loopback; }
    // calcc info[6] (the classic scheck bits) in words, "" when 0.
    static QString scheckText(int info6);
    // Why a calcc calibration failed: info[0..3] and info[6] in words, "" = none.
    static QString calcFailText(const std::array<int, 16>& info);
    // Profiles: replace every setting at once (and apply).
    void applySettings(const Settings& s);
    void setTwoToneParams(double f1, double f2, double mag);
    void setTwoTone(bool on);
    bool saveCorrection(const QString& file);
    bool restoreCorrection(const QString& file);

    const Settings& settings() const { return m_s; }
    bool twoToneOn() const { return m_twoTone; }
    Status status() const;

    // AM/AM, AM/PM of the last calibration and the correction curves.
    struct Disp {
        std::vector<double> x, ym, yc, ys;   // nsamps
        std::vector<double> xm, ymc, xa, yac;   // correction, cpts
        double phsRefDeg = 0.0;
    };
    bool display(Disp& out) const;

    // The last kSnapPairs feedback samples (coupler, DDC0) as fed to pscc,
    // contiguous, interleaved I/Q, oldest first; false until that many have
    // been fed since the last key-down. For the IMD measurement in Amp View.
    static constexpr int kSnapPairs = 16384;
    bool feedbackSnapshot(std::vector<double>& rxIq) const;
    // Both streams (tx = DDC1 reference, rx = DDC0 feedback), same layout.
    bool pairSnapshot(std::vector<double>& txIq, std::vector<double>& rxIq) const;

signals:
    void statusChanged();
    void settingsChanged();   // applySettings() (a TX profile)

private:
    void load();
    void save() const;
    void pushTransport();
    void applyWdspParams();
    void tick();
    void control(int reset, int mancal, int automode, int turnon);
    void updateFeedGate();

    Settings m_s;
    QObject* m_transport = nullptr;
    QObject* m_txDsp = nullptr;
    QTimer* m_timer = nullptr;
    int m_channel = -1;
    bool m_keyed = false;
    bool m_cw = false;
    bool m_twoTone = false;

    // Thetis auto-attenuate state machine.
    enum class AaState { Monitor, SetNewValues, Restore } m_aa = AaState::Monitor;
    int m_aaDelta = 0;
    int m_aaSaveAuto = 0, m_aaSaveSingle = 0;
    // After an attenuator change: when, and calibrations completed since --
    // the next one or two still carry the old level (log 22:36: "feedback
    // 29" at 31 dB and again at 17 dB -> stepped to 3 dB, the ADC clipped).
    long long m_aaChangedMs = 0;
    int m_aaAttemptsSince = 100;
    std::array<int, 16> m_info{};
    std::array<int, 16> m_oldInfo{};

    // I/O thread.
    std::atomic<bool> m_feedOk{false};
    std::atomic<bool> m_conjugate{false};
    std::atomic<int> m_feedChannel{-1};
    std::atomic<unsigned long long> m_pairs{0};
    std::atomic<long long> m_lastFeedMs{0};
    std::atomic<double> m_hwMeasured{0.0};
    std::atomic<long long> m_hwMeasuredMs{0};
    int m_hwVotes = 0;
    long long m_hwLastUsedMs = 0;
    bool m_singleRunning = false;
    static constexpr int kBlock = 1024;
    std::vector<double> m_accTx, m_accRx;
    std::vector<double> m_flipTx, m_flipRx;   // raw-DDC A/B copies (I/O thread)
    int m_accN = 0;
    // The transport (overload latch), set before it starts.
    class P2Transport* m_p2 = nullptr;
    double m_fbEma = 0.0;                  // feedback level, smoothed over calibrations (0 = none)
    double m_scheckSeen = 0.0;
    // Heat drift of the amp delay (anan162, see heatTick()).
    void heatTick();
    double effectiveDelayNs() const;
    double m_heat = 0.0;            // PA heat state (1 = settled at drive 108)
    double m_heatAtMeas = 0.0;      // the heat state the band's delay was measured at
    double m_delayAppliedNs = NAN;  // what SetPSTXDelay was last given
    long long m_heatLastMs = 0;
    long long m_heatLogMs = 0;   // WDSP scheck failures already logged (GetPSScheckDiag)
    // Feedback ring for feedbackSnapshot() (written on the I/O thread).
    mutable std::mutex m_snapMutex;
    std::vector<double> m_snap;      // 2 * kSnapPairs
    std::vector<double> m_snapTx;    // 2 * kSnapPairs, the DDC1 reference
    int m_snapPos = 0;
    int m_snapFill = 0;
    void measureDelay();
    void measureImd();
    double m_imd3Host = NAN, m_imd3Ref = NAN, m_imd3Fb = NAN;
    double m_delayNs = 0.0, m_delayCorr = 0.0;
    double m_delayLoggedNs = 1e9;
    bool m_loopback = false;
    double m_compDb = NAN;
    QString m_band, m_measBand;
    void loadBandDelay();
    // Settings key of the band's delay memory for the current feedback ADC.
    QString delayKey(const char* base, const QString& bandK) const;
    void measTick();
    void measFinish(const QString& result, bool ok);
    int m_measStep = -1;
    long long m_measStartMs = 0;
    unsigned m_delaySeq = 0, m_measSeqSeen = 0;   // successful measureDelay() calls
    std::vector<double> m_measDelays, m_measComps;
    std::vector<double> m_measFitDelays, m_measFitRes, m_measFitResOld;
    double m_delayMeasuredCompDb = NAN;
    QString m_measResult;
    bool m_measOk = true;
    bool m_delayMeasured = false;
    QString m_delayMeasuredAt;
    long long m_delayLastMs = 0;
    int m_lastCalLogged = -1;
    int m_lastStateLogged = -1;
    // calcc health: our own resets (not to be blamed on calcc), counts, the
    // memoryless fit of the measurement (residual at the old / new delay).
    long long m_ownResetMs = 0;
    unsigned m_calcFails = 0, m_calcDrops = 0;
    QString m_lastCalcFail;
    double m_fitDelayNs = NAN, m_fitResDb = NAN, m_fitResOldDb = NAN;
    void fitDelay(const std::vector<double>& tx, const std::vector<double>& rx);
};

} // namespace AetherSDR::anan
