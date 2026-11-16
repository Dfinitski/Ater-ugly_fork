// AetherSDR ANAN fork, build 452 (operator, 16.11): one set of receiver
// settings for the three places that remember them -- the band buttons (band
// memory, MainWindow::selectBand), the quick-memory buttons (band stack panel)
// and the station memories. Each kept its own subset (the band buttons the
// antennas, LNA and TX filter; the quick memories volume, NB / NR, WNB and the
// receiver rate; the memories the step, squelch, FM repeater and RTTY / DIG
// offsets); now each stores this one snapshot and recalls it the same way
// (MainWindow::applyRecallSnapshot). A station memory keeps its own frequency
// and name (and its mode / filter, which the memory editor can change).
// Build 453 (operator, 16.11): no tuning step in the snapshot -- the step is
// the mode group's only (RadioModel, build 445); a recall that set it would
// rewrite the group's step.
//
// Only for radios without a command plane (ANAN / HL2: the program is the
// band stack); a Flex keeps its radio-side band stack and memories.
#pragma once

#include "core/AppSettings.h"
#include "core/backends/anan/AnanUiState.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace AetherSDR::RecallSnapshot {

inline QJsonObject capture(const RadioModel& model, const SliceModel* s)
{
    QJsonObject o;
    auto& rm = const_cast<RadioModel&>(model);
    if (!s || !(s->frequency() > 0.0) || rm.usesFlexCommandPlane())
        return o;
    auto& st = AppSettings::instance();
    // Receiver
    o.insert(QStringLiteral("freq"), s->frequency());
    o.insert(QStringLiteral("mode"), s->mode());
    o.insert(QStringLiteral("lo"), s->filterLow());
    o.insert(QStringLiteral("hi"), s->filterHigh());
    // No tuning step (build 453): it belongs to the mode group (build 445).
    o.insert(QStringLiteral("agc"), s->agcMode());
    o.insert(QStringLiteral("agcT"), s->agcThreshold());
    o.insert(QStringLiteral("vol"), static_cast<int>(s->audioGain()));
    o.insert(QStringLiteral("nb"), s->nbOn());
    o.insert(QStringLiteral("nbL"), s->nbLevel());
    o.insert(QStringLiteral("nr"), s->nrOn());
    o.insert(QStringLiteral("nrL"), s->nrLevel());
    o.insert(QStringLiteral("sq"), s->squelchOn());
    o.insert(QStringLiteral("sqL"), s->squelchLevel());
    o.insert(QStringLiteral("rttyM"), s->rttyMark());
    o.insert(QStringLiteral("rttyS"), s->rttyShift());
    o.insert(QStringLiteral("diglO"), s->diglOffset());
    o.insert(QStringLiteral("diguO"), s->diguOffset());
    o.insert(QStringLiteral("rptDir"), s->repeaterOffsetDir());
    o.insert(QStringLiteral("rptOff"), s->fmRepeaterOffsetFreq());
    o.insert(QStringLiteral("toneMode"), s->fmToneMode());
    o.insert(QStringLiteral("toneVal"), s->fmToneValue());
    // Panadapter: the view (centre, width), the receiver rate, WNB and the
    // receiver's attenuator / gain.
    o.insert(QStringLiteral("rate"), anan::AnanUiState::instance().sampleRateHz);
    if (auto* pan = rm.panadapter(s->panId())) {
        o.insert(QStringLiteral("bw"), rm.effectivePanBandwidthMhz(pan->panId()));
        o.insert(QStringLiteral("center"), rm.effectivePanCenterMhz(pan->panId()));
        o.insert(QStringLiteral("rfGain"), pan->rfGain());
        o.insert(QStringLiteral("wnb"), pan->wnbActive());
        o.insert(QStringLiteral("wnbL"), pan->wnbLevel());
    }
    if (s->sliceId() == 0) {
        // The transmit path: the SSB TX filter, the Alex antennas and the LNA.
        const TransmitModel& tx = rm.transmitModel();
        o.insert(QStringLiteral("txLo"), tx.txFilterLow());
        o.insert(QStringLiteral("txHi"), tx.txFilterHigh());
        o.insert(QStringLiteral("rxAnt"), st.value(QStringLiteral("AnanRxAnt"), 0).toInt());
        o.insert(QStringLiteral("txAnt"), st.value(QStringLiteral("AnanTxAnt"), 0).toInt());
        o.insert(QStringLiteral("lna"), st.value(QStringLiteral("AnanLna"), false).toBool());
    }
    return o;
}

inline QString toText(const QJsonObject& o)
{
    return o.isEmpty() ? QString()
                       : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

inline QJsonObject fromText(const QString& text)
{
    return text.isEmpty() ? QJsonObject()
                          : QJsonDocument::fromJson(text.toUtf8()).object();
}

} // namespace AetherSDR::RecallSnapshot
