#pragma once

// ANAN-100D port: per-receiver attenuator state shared between Hl2Backend
// (ANAN mode) and the panadapter ANT panels. Everything here is touched on the
// GUI thread only (the backend's timers and the panels both live there), so it
// is a plain singleton with no locking and no signals: each side polls.
//
// Rule (operator, 28.10.2026): a receiver controls the step attenuator of the
// ADC it listens to. When both receivers use the same ADC, receiver 1 owns it
// and receiver 2's ATT / AUTO / Floor are read-only.

#include "core/AppSettings.h"

#include <QHash>
#include <functional>
#include <algorithm>
#include <limits>
#include <QString>

namespace AetherSDR::anan {

// ANAN-100D port (operator, 31.10): the outer 10 % of the DDC span on each
// side is the DDC filter's roll-off. The spectrum is cut to the inner 80 %
// first thing after the FFT, so those dips are neither shown nor seen by the
// AUTO attenuator; the widest view (zoom x1.0) is this fraction of the rate.
inline constexpr double kUsableSpanFraction = 0.8;

class AnanUiState {
public:
    static AnanUiState& instance()
    {
        static AnanUiState s;
        return s;
    }
    AnanUiState()
    {
        for (int rx = 0; rx < 2; ++rx)
            autoWanted[rx] = AppSettings::instance()
                                 .value(autoKey(rx), QStringLiteral("False"))
                                 .toString() == QLatin1String("True");
        auto& st = AppSettings::instance();
        sMeterCorrDb = std::clamp(st.value(QStringLiteral("AnanSMeterCorrDb"), 0.0).toDouble(), -50.0, 50.0);
        sMeterUseAtt = st.value(QStringLiteral("AnanSMeterUseAtt"), QStringLiteral("False")).toString()
                       == QLatin1String("True");
        sMeterUseLna = st.value(QStringLiteral("AnanSMeterUseLna"), QStringLiteral("False")).toString()
                       == QLatin1String("True");
        sMeterLnaGainDb = std::clamp(st.value(QStringLiteral("AnanSMeterLnaGainDb"), 0).toInt(), 0, 99);
        sMeterPeak = st.value(QStringLiteral("AnanSMeterDetector"), QStringLiteral("Peak")).toString()
                     != QLatin1String("Average");
        showNoiseLine = st.value(QStringLiteral("AnanShowNoiseLine"), QStringLiteral("True")).toString()
                        == QLatin1String("True");
        for (int rx = 0; rx < 2; ++rx) {
            agcAuto[rx] = st.value(agcAutoKey(rx), QStringLiteral("False")).toString() == QLatin1String("True");
            agcNfOffsetDb[rx] = std::clamp(st.value(agcOffsetKey(rx), kAgcNfOffsetDefaultDb).toInt(),
                                           kAgcNfOffsetMinDb, kAgcNfOffsetMaxDb);
        }
    }

    // AGC follows the noise floor (operator, 04.11; Thetis "Auto AGC",
    // console.cs tmrAutoAGC_Tick): per receiver, the AGC threshold is the
    // measured noise floor + this offset, so the noise heard in the pauses
    // stays the same whatever the antenna noise and the attenuator do.
    // Manual (off): AGC-T sets the gain as before. Read by Hl2Backend.
    // Thetis' default is +20 on its calibrated display. Here a floor at the
    // AUTO ATT target (-120 dBm) with +20 gives ~101 dB of AGC gain against
    // the 79 dB of the default manual AGC-T 65 -- 22 dB more hiss. +40 keeps
    // today's loudness within 2 dB at the target.
    static constexpr int kAgcNfOffsetDefaultDb = 40;
    static constexpr int kAgcNfOffsetMinDb = -20;
    static constexpr int kAgcNfOffsetMaxDb = 60;
    bool agcAuto[2] = {false, false};
    int agcNfOffsetDb[2] = {kAgcNfOffsetDefaultDb, kAgcNfOffsetDefaultDb};
    // Written by the backend: the AGC gain the floor law gives now (NaN = none).
    double agcAutoTopDb[2] = {std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::quiet_NaN()};
    // The same gain in AGC-T units (0..100, the manual slider's scale), so the
    // AGC-T slider can show where Auto has put it (as Thetis moves AGC-T);
    // -1 = none. Also the measured floor it came from (NaN = none).
    int agcAutoT[2] = {-1, -1};
    // Written by the backend (06.11): the noise floor in dBm / 500 Hz at the
    // ADC on the meter's calibrated scale (drawn as a line; not corrected for
    // the attenuator), and the receiver's own noise the same way (captured in
    // Setup > Receive; NaN = never captured). AUTO ATT holds the floor at
    // own + the margin.
    double shownFloorDbm[2] = {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::quiet_NaN()};
    double ownFloorDbm[2] = {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN()};
    // When shownFloorDbm was last written (ms since epoch): the panadapter
    // draws the line only while it is fresh (not after a disconnect).
    qint64 shownFloorAtMs[2] = {0, 0};
    // ADC overload (06.11): when each ADC was last reported clipping (ms
    // since epoch, by the backend), which ADC each receiver listens to, and
    // a test switch (Setup > Receive) that shows the warning without one.
    qint64 adcOverloadAtMs[2] = {0, 0};
    int rxAdc[2] = {0, 0};
    // Build 277: receive ADCs of the radio as driven (by the backend, from
    // the board profile). With one, the ADC1/ADC2 choice is not offered.
    int numAdcs = 2;
    bool adcOverloadTest = false;
    // What the panadapter adds to a per-bin level to draw it (06.11: the
    // referral of a bin to 500 Hz plus the meter's calibration); lines that
    // are mapped on the per-bin scale (the amsq squelch line) add it too.
    double panShiftDb[2] = {0.0, 0.0};
    double agcAutoFloorDbm[2] = {std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::quiet_NaN()};
    [[nodiscard]] static QString agcAutoKey(int rx)
    {
        return rx == 1 ? QStringLiteral("AnanRx2AgcAuto") : QStringLiteral("AnanRx1AgcAuto");
    }
    [[nodiscard]] static QString agcOffsetKey(int rx)
    {
        return rx == 1 ? QStringLiteral("AnanRx2AgcNfOffsetDb") : QStringLiteral("AnanRx1AgcNfOffsetDb");
    }
    void setAgcAuto(int rx, bool on, int offsetDb)
    {
        if (rx < 0 || rx > 1)
            return;
        agcAuto[rx] = on;
        agcNfOffsetDb[rx] = std::clamp(offsetDb, kAgcNfOffsetMinDb, kAgcNfOffsetMaxDb);
        auto& st = AppSettings::instance();
        st.setValue(agcAutoKey(rx), on ? QStringLiteral("True") : QStringLiteral("False"));
        st.setValue(agcOffsetKey(rx), QString::number(agcNfOffsetDb[rx]));
        st.save();
    }

    // S-meter reading (Setup > Receive, operator 03.11), applied to the
    // meter only (not the spectrum): a fixed correction, plus the step
    // attenuator in front of the ADC (the reading refers to the antenna),
    // minus the LNA gain -- only while the LNA is switched on (the LNA
    // button in the panadapter ANT panel, "AnanLna"; operator 04.11).
    double sMeterCorrDb = 0.0;
    bool sMeterUseAtt = false;
    bool sMeterUseLna = false;
    int sMeterLnaGainDb = 0;
    // Detector (Thetis' two receive meter modes): Peak "Signal" (default, as
    // Thetis) or Average "Sig Avg". See Hl2RxDsp::setSMeterPeak.
    bool sMeterPeak = true;
    void setSMeterDetector(bool peak)
    {
        sMeterPeak = peak;
        auto& st = AppSettings::instance();
        st.setValue(QStringLiteral("AnanSMeterDetector"), peak ? QStringLiteral("Peak") : QStringLiteral("Average"));
        st.save();
    }

    [[nodiscard]] double sMeterOffsetDb(int attDb, bool lnaOn) const
    {
        return sMeterCorrDb + (sMeterUseAtt ? attDb : 0) - ((sMeterUseLna && lnaOn) ? sMeterLnaGainDb : 0);
    }
    void setSMeter(double corrDb, bool useAtt, bool useLna, int lnaGainDb)
    {
        sMeterCorrDb = std::clamp(corrDb, -50.0, 50.0);
        sMeterUseAtt = useAtt;
        sMeterUseLna = useLna;
        sMeterLnaGainDb = std::clamp(lnaGainDb, 0, 99);
        auto& st = AppSettings::instance();
        st.setValue(QStringLiteral("AnanSMeterCorrDb"), QString::number(sMeterCorrDb, 'f', 1));
        st.setValue(QStringLiteral("AnanSMeterUseAtt"), useAtt ? QStringLiteral("True") : QStringLiteral("False"));
        st.setValue(QStringLiteral("AnanSMeterUseLna"), useLna ? QStringLiteral("True") : QStringLiteral("False"));
        st.setValue(QStringLiteral("AnanSMeterLnaGainDb"), QString::number(sMeterLnaGainDb));
        st.save();
    }

    // Written by the backend: which receiver (0 = RX1, 1 = RX2) a pan shows,
    // and whether that receiver owns its ADC's attenuator.
    QHash<QString, int> panRx;
    // The DDC rate in Hz (0 = not connected / not an ANAN). The pan's
    // bandwidth is the display zoom; the rate label shows this.
    int sampleRateHz = 0;
    QHash<QString, bool> panOwnsAtt;
    // AUTO requested per receiver, persisted ("AnanRx1AutoAtt" /
    // "AnanRx2AutoAtt"). Whether it may RUN is decided by the backend from
    // the ADC ownership rule when the receiver comes up; the wish is kept.
    bool autoWanted[2] = {false, false};

    void setAutoWanted(int rx, bool on)
    {
        if (rx < 0 || rx > 1)
            return;
        autoWanted[rx] = on;
        AppSettings::instance().setValue(autoKey(rx), on ? QStringLiteral("True")
                                                         : QStringLiteral("False"));
        AppSettings::instance().save();
    }
    [[nodiscard]] static QString autoKey(int rx)
    {
        return rx == 1 ? QStringLiteral("AnanRx2AutoAtt") : QStringLiteral("AnanRx1AutoAtt");
    }

    // The widgets know a pan by the model's neutral id ("0xe1000000"), the
    // backend by its own ("hl2-0"); RadioModel installs the translation
    // (RadioModel::backendPanIdFor). Without it every lookup missed: AUTO
    // ATT never latched, and the slice flag fell back to the slice number.
    std::function<QString(const QString&)> panIdToBackend;
    [[nodiscard]] QString backendPan(const QString& panId) const
    {
        if (panRx.contains(panId) || !panIdToBackend)
            return panId;
        return panIdToBackend(panId);
    }
    [[nodiscard]] int rxForPan(const QString& panId) const { return panRx.value(backendPan(panId), -1); }
    [[nodiscard]] bool ownsAtt(const QString& panId) const { return panOwnsAtt.value(backendPan(panId), true); }

    // The noise-floor line on the panadapter (Setup > Receive, 06.11).
    bool showNoiseLine = true;
    void setShowNoiseLine(bool on)
    {
        showNoiseLine = on;
        auto& st = AppSettings::instance();
        st.setValue(QStringLiteral("AnanShowNoiseLine"), on ? QStringLiteral("True") : QStringLiteral("False"));
        st.save();
    }
    // The receiver's own noise per ADC (operator, 06.11: on 40 m the antenna
    // noise is far above it even at ATT 31 dB, so it cannot be measured on
    // the air). Captured on request (Setup > Receive) with the antenna
    // disconnected, at ATT 31 dB; dBm / 500 Hz at the ADC, WITHOUT the
    // meter's calibration. The GUI sets ownNoiseCaptureRequested; the
    // backend measures, stores the key and writes ownNoiseStatus.
    bool ownNoiseCaptureRequested = false;
    QString ownNoiseStatus;
    [[nodiscard]] static QString ownNoiseKey(int adc)
    {
        return adc == 1 ? QStringLiteral("AnanOwnNoiseDbmAdc2") : QStringLiteral("AnanOwnNoiseDbmAdc1");
    }
    [[nodiscard]] static QString ownNoiseDateKey(int adc)
    {
        return adc == 1 ? QStringLiteral("AnanOwnNoiseDateAdc2") : QStringLiteral("AnanOwnNoiseDateAdc1");
    }

    // AppSettings keys of the AUTO ATT margin per receiver: how many dB the
    // band noise is held above the receiver's own noise (06.11; it replaced
    // the absolute "AnanAutoAttTargetDbm"). The receiver's own noise then
    // costs 10 log10(1 + 10^(-margin/10)) dB of sensitivity: +3 dB 1.8,
    // +6 dB 1.0, +10 dB 0.4, +20 dB 0.04.
    static constexpr int kAutoAttMarginDefaultDb = 10;
    static constexpr int kAutoAttMarginMaxDb = 30;
    [[nodiscard]] static QString autoAttMarginKey(int rx)
    {
        return rx == 1 ? QStringLiteral("AnanAutoAttMarginDbRx2")
                       : QStringLiteral("AnanAutoAttMarginDb");
    }
};

}  // namespace AetherSDR::anan
