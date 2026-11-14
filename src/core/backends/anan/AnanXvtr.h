#pragma once

// ANAN fork build 317 (operators, 12.11: "users cannot set up and use the
// transverter mode"). On a FlexRadio the transverters live in the radio: it
// stores them, reports them as `xvtr N` status objects, and moves its own
// oscillators. An openHPSDR radio has none of that, so the transverter list is
// kept here (AppSettings "AnanTransverters", JSON), reported to RadioModel in
// the same `xvtr N` form so the Transverters setup page and the band menu work
// unchanged, and applied by Hl2Backend where a frequency becomes a register
// value (ncoCommandHz, band filters).
//
// A transverter maps RF to IF as SmartSDR does: rf_freq is the RF frequency
// that appears at if_freq, so IF = RF - (rf_freq - if_freq) - lo_error. It is
// used for frequencies beyond the radio's own coverage whose IF falls inside
// it; with several candidates, the one whose rf_freq is nearest.

#include "core/AppSettings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>

namespace AetherSDR::anan {

struct Xvtr {
    int index{0};
    int order{0};
    QString name;
    double rfMhz{0.0};
    double ifMhz{0.0};
    double loErrorMhz{0.0};
    double rxGainDb{0.0};
    double maxPowerDbm{10.0};   // FlexRadio's field; not used on an openHPSDR radio
    // Build 357: the ceiling of the RF Power and Tune sliders while the
    // transmitter is on this transverter, in percent of the calibrated full
    // power. A percentage, not dBm: the program cannot know which connector
    // drives the transverter (the PA, or the low-level output ahead of it) and
    // so has no dBm figure of its own to compare against.
    double maxDrivePct{100.0};
    bool rxOnly{false};
};

class AnanXvtrStore {
public:
    static AnanXvtrStore& instance()
    {
        static AnanXvtrStore s;
        return s;
    }

    [[nodiscard]] const QMap<int, Xvtr>& all() const { return m_items; }

    // Usable: both frequencies set, the IF inside the radio's coverage and the
    // RF beyond it.
    [[nodiscard]] static bool valid(const Xvtr& x, double nativeMinHz, double nativeMaxHz)
    {
        const double ifHz = x.ifMhz * 1.0e6;
        return x.rfMhz > 0.0 && x.ifMhz > 0.0 && ifHz >= nativeMinHz && ifHz <= nativeMaxHz
               && x.rfMhz * 1.0e6 > nativeMaxHz;
    }

    // The transverter that carries `hz` (RF), or nullptr: `hz` beyond the
    // radio's own coverage and its IF inside it.
    [[nodiscard]] const Xvtr* forRf(double hz, double nativeMinHz, double nativeMaxHz) const
    {
        if (hz <= nativeMaxHz)
            return nullptr;
        const Xvtr* best = nullptr;
        double bestDist = std::numeric_limits<double>::max();
        for (const Xvtr& x : m_items) {
            if (!valid(x, nativeMinHz, nativeMaxHz))
                continue;
            const double ifHz = hz - offsetHz(x);
            if (ifHz < nativeMinHz || ifHz > nativeMaxHz)
                continue;
            const double dist = std::abs(hz - x.rfMhz * 1.0e6);
            if (dist < bestDist) {
                bestDist = dist;
                best = &x;
            }
        }
        return best;
    }

    // RF - IF for this transverter (the LO, with its error), Hz.
    [[nodiscard]] static double offsetHz(const Xvtr& x)
    {
        return (x.rfMhz - x.ifMhz + x.loErrorMhz) * 1.0e6;
    }

    int create()
    {
        int idx = 0;
        while (m_items.contains(idx))
            ++idx;
        Xvtr x;
        x.index = idx;
        x.order = idx;
        x.name = QStringLiteral("X%1").arg(idx);
        m_items.insert(idx, x);
        save();
        return idx;
    }
    void remove(int idx)
    {
        m_items.remove(idx);
        save();
    }
    Xvtr* find(int idx)
    {
        auto it = m_items.find(idx);
        return it == m_items.end() ? nullptr : &it.value();
    }
    void save()
    {
        QJsonArray arr;
        for (const Xvtr& x : m_items) {
            QJsonObject o;
            o[QStringLiteral("index")] = x.index;
            o[QStringLiteral("order")] = x.order;
            o[QStringLiteral("name")] = x.name;
            o[QStringLiteral("rf")] = x.rfMhz;
            o[QStringLiteral("if")] = x.ifMhz;
            o[QStringLiteral("loError")] = x.loErrorMhz;
            o[QStringLiteral("rxGain")] = x.rxGainDb;
            o[QStringLiteral("maxPower")] = x.maxPowerDbm;
            o[QStringLiteral("maxPct")] = x.maxDrivePct;
            o[QStringLiteral("rxOnly")] = x.rxOnly;
            arr.append(o);
        }
        auto& st = AppSettings::instance();
        st.setValue(QStringLiteral("AnanTransverters"),
                    QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        st.save();
    }

    // The status object RadioModel::onStatusReceived parses for `xvtr N`.
    [[nodiscard]] static QMap<QString, QString> statusKvs(const Xvtr& x, double nativeMinHz,
                                                          double nativeMaxHz)
    {
        QMap<QString, QString> kvs;
        kvs[QStringLiteral("order")] = QString::number(x.order);
        kvs[QStringLiteral("name")] = x.name;
        kvs[QStringLiteral("rf_freq")] = QString::number(x.rfMhz, 'f', 6);
        kvs[QStringLiteral("if_freq")] = QString::number(x.ifMhz, 'f', 6);
        kvs[QStringLiteral("lo_error")] = QString::number(x.loErrorMhz, 'f', 6);
        kvs[QStringLiteral("rx_gain")] = QString::number(x.rxGainDb, 'f', 2);
        kvs[QStringLiteral("max_power")] = QString::number(x.maxDrivePct, 'f', 2);   // build 357: percent
        kvs[QStringLiteral("rx_only")] = x.rxOnly ? QStringLiteral("1") : QStringLiteral("0");
        kvs[QStringLiteral("is_valid")] =
            valid(x, nativeMinHz, nativeMaxHz) ? QStringLiteral("1") : QStringLiteral("0");
        return kvs;
    }

private:
    AnanXvtrStore()
    {
        const QString json = AppSettings::instance()
                                 .value(QStringLiteral("AnanTransverters"), QString())
                                 .toString();
        const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
        for (const auto& v : arr) {
            const QJsonObject o = v.toObject();
            Xvtr x;
            x.index = o.value(QStringLiteral("index")).toInt();
            x.order = o.value(QStringLiteral("order")).toInt(x.index);
            x.name = o.value(QStringLiteral("name")).toString();
            x.rfMhz = o.value(QStringLiteral("rf")).toDouble();
            x.ifMhz = o.value(QStringLiteral("if")).toDouble();
            x.loErrorMhz = o.value(QStringLiteral("loError")).toDouble();
            x.rxGainDb = o.value(QStringLiteral("rxGain")).toDouble();
            x.maxPowerDbm = o.value(QStringLiteral("maxPower")).toDouble(10.0);
            x.maxDrivePct = std::clamp(o.value(QStringLiteral("maxPct")).toDouble(100.0), 0.0, 100.0);
            x.rxOnly = o.value(QStringLiteral("rxOnly")).toBool();
            m_items.insert(x.index, x);
        }
    }

    QMap<int, Xvtr> m_items;
};

} // namespace AetherSDR::anan
