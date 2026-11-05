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

    // AppSettings keys of the AUTO noise-floor target per receiver.
    [[nodiscard]] static QString floorTargetKey(int rx)
    {
        return rx == 1 ? QStringLiteral("AnanAutoAttTargetDbmRx2")
                       : QStringLiteral("AnanAutoAttTargetDbm");
    }
};

}  // namespace AetherSDR::anan
