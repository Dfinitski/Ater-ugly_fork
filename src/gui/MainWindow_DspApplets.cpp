// MainWindow_DspApplets.cpp — client-DSP applet wiring: wirePooDooTiles() and
// wireDspApplets() (two methods because constructor chrome wiring runs between
// them). Covers PooDoo RX chain tiles, P/CW and PHNE applets, the client DSP
// applet family (TX and RX), TX chain + PUDU monitor, and RX chain edit/bypass.
// Window-side, runs once at construction.

#include "MainWindow.h"

#include "AetherDspWidget.h"
#include "AppletPanel.h"
#include "AetherialAudioStrip.h"
#include "ClientCompApplet.h"
#include "ClientCompEditor.h"
#include "ClientDeEssApplet.h"
#include "ClientEqApplet.h"
#include "ClientEqEditor.h"
#include "ClientGateApplet.h"
#include "ClientGateEditor.h"
#include "ClientPuduApplet.h"
#include "ClientPuduEditor.h"
#include "ClientReverbApplet.h"
#include "ClientRxDspApplet.h"
#include "ClientTubeApplet.h"
#include "ClientTubeEditor.h"
#include "EqApplet.h"
#include "PhoneCwApplet.h"
#include "TitleBar.h"
#include "ClientChainApplet.h"
#include "MainWindowHelpers.h"
#include "core/AetherDspModePolicy.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/backends/anan/AnanUiState.h"
#include "core/ClientComp.h"
#include "core/ClientEq.h"
#include "core/HostVoiceChainPolicy.h"
#include "core/LogManager.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QTimer>
#include <QMessageBox>

#include <memory>

#include <algorithm>
#include <array>
#include <cmath>

namespace AetherSDR {

MainWindow::TxAudioPathBlock MainWindow::txAudioPathBlock() const
{
    const bool pcAudioOn = AppSettings::instance()
        .value("PcAudioEnabled", "True").toString() == "True";
    return classifyTxAudioPath(m_radioModel.isConnected(),
                              m_radioModel.backendCapabilities(), pcAudioOn,
                              m_radioModel.transmitModel().micSelection());
}

QString MainWindow::txAudioPathBlockMessage(TxAudioPathBlock block) const
{
    switch (block) {
    case TxAudioPathBlock::PcAudio:
        return tr("PC Audio is off, so this radio cannot receive AetherTX audio "
                  "from your computer. Connect your microphone to the computer "
                  "and turn on PC Audio in the main window. A mic on the radio "
                  "bypasses AetherTX.");
    case TxAudioPathBlock::MicInput:
        return tr("AetherTX uses a microphone connected to this computer. "
                  "A mic plugged into the radio bypasses its effects. "
                  "In Phone/CW, set Microphone source to PC.");
    case TxAudioPathBlock::None:
        return {};
    }
    return {};
}

bool MainWindow::showTxAudioPathErrorIfBlocked()
{
    const TxAudioPathBlock block = txAudioPathBlock();
    if (block == TxAudioPathBlock::None) return false;
    if (QMessageBox* existing = findChild<QMessageBox*>(
            QStringLiteral("aetherTxAudioPathError"))) {
        existing->raise();
        existing->activateWindow();
        return true;
    }
    auto* error = new QMessageBox(
        QMessageBox::Warning, tr("AetherTX unavailable"),
        txAudioPathBlockMessage(block)
            + tr("\n\nFix the audio path, then open AetherTX again."),
        QMessageBox::Ok, this);
    error->setObjectName(QStringLiteral("aetherTxAudioPathError"));
    error->setAccessibleName(tr("AetherTX audio path error"));
    error->setAttribute(Qt::WA_DeleteOnClose);
    error->open();
    return true;
}

void MainWindow::updateTxAudioPathNotice()
{
    const bool pcAudioOn = AppSettings::instance()
        .value("PcAudioEnabled", "True").toString() == "True";
    QString detail;
    QString compact;
    bool warning = false;

    if (!m_radioModel.isConnected()) {
        if (!pcAudioOn) {
            detail = tr("PC Audio is off. AetherTX processes a microphone "
                        "connected to this computer; a mic plugged into the "
                        "radio bypasses its effects. Connect a radio to see "
                        "its audio routing requirements.");
            compact = tr("PC Audio is off. A radio mic bypasses AetherTX; "
                         "use a computer mic.");
        }
    } else {
        const TxAudioPathBlock block = txAudioPathBlock();
        detail = txAudioPathBlockMessage(block);
        warning = block != TxAudioPathBlock::None;
        if (block == TxAudioPathBlock::PcAudio) {
            compact = tr("PC Audio is off. Use a computer mic and turn on "
                         "PC Audio for AetherTX.");
        } else if (block == TxAudioPathBlock::MicInput) {
            compact = tr("A radio mic bypasses AetherTX. In Phone/CW, "
                         "set Microphone source to PC.");
        }
    }

    if (m_aetherialStrip) {
        m_aetherialStrip->setAudioPathNotice(detail, warning);
    }
    if (m_appletPanel && m_appletPanel->clientChainApplet()) {
        m_appletPanel->clientChainApplet()->setTxAudioPathNotice(compact, warning);
        m_appletPanel->setTxAudioPathBlocked(warning);
    }
    // A live route change must not leave the TX editor usable after it
    // becomes unavailable. The applet gives the operator the recovery path.
    if (warning && m_aetherialStrip) {
        m_aetherialStrip->closeSettingsIfOpen();
        if (m_aetherialStrip->isVisible()) {
            m_aetherialStrip->hide();
            updateToolsMenuState();
        }
    }
    if (warning) {
        if (m_clientEqEditor && m_clientEqEditor->isShowingTx()) m_clientEqEditor->hide();
        if (m_clientGateEditor && m_clientGateEditor->isShowingTx()) m_clientGateEditor->hide();
        if (m_clientCompEditor && m_clientCompEditor->isShowingTx()) m_clientCompEditor->hide();
        if (m_clientTubeEditor && m_clientTubeEditor->isShowingTx()) m_clientTubeEditor->hide();
        if (m_clientPuduEditor && m_clientPuduEditor->isShowingTx()) m_clientPuduEditor->hide();
    }
}

QString MainWindow::activeAetherDspMethod() const
{
    if (!m_audio) {
        return {};
    }
    if (m_audio->nr2Enabled()) {
        return QStringLiteral("NR2");
    }
    if (m_audio->nr4Enabled()) {
        return QStringLiteral("NR4");
    }
    if (m_audio->mnrEnabled()) {
        return QStringLiteral("MNR");
    }
    if (m_audio->dfnrEnabled()) {
        return QStringLiteral("DFNR");
    }
    if (m_audio->rn2Enabled()) {
        return QStringLiteral("RN2");
    }
    if (m_audio->nvAfxEnabled()) {
        return QStringLiteral("BNR");
    }
    if (m_audio->nnrEnabled()) {
        return QStringLiteral("NNR");
    }
    return {};
}

void MainWindow::setAetherDspMethodEnabled(const QString& method, bool enabled)
{
    if (!m_audio || method.isEmpty()) {
        return;
    }
    if (method == QStringLiteral("NR2") && enabled) {
        enableNr2WithWisdom();
        return;
    }

    QMetaObject::invokeMethod(m_audio, [audio = m_audio, method, enabled]() {
        if (method == QStringLiteral("NR2")) {
            audio->setNr2Enabled(enabled);
        } else if (method == QStringLiteral("NR4")) {
            audio->setNr4Enabled(enabled);
        } else if (method == QStringLiteral("MNR")) {
            audio->setMnrEnabled(enabled);
        } else if (method == QStringLiteral("DFNR")) {
            audio->setDfnrEnabled(enabled);
        } else if (method == QStringLiteral("RN2")) {
            audio->setRn2Enabled(enabled);
        } else if (method == QStringLiteral("BNR")) {
            audio->setNvAfxEnabled(enabled);
        } else if (method == QStringLiteral("NNR")) {
            audio->setNnrEnabled(enabled);
        }
    });
}

void MainWindow::updateAetherDspModePolicy()
{
    if (!m_audio) {
        return;
    }

    QList<AetherDspSliceAudioState> sliceStates;
    for (SliceModel* slice : m_radioModel.slices()) {
        if (!slice) {
            continue;
        }
        sliceStates.append({
            slice->mode(),
            slice->audioMute(),
            slice->audioGain(),
            slice->inCapture(),
        });
    }

    const bool restricted = aetherDspMixRequiresDisable(sliceStates);
    // ANAN fork build 591: the mix rule is for the post-processing method of
    // the mixed audio; NR2 / NNR / DFNR are each receiver's and stop on that
    // receiver's own CW / digital mode (ananEmnrPushAll), not on another's.
    const AetherDspModePolicy::Action action =
        m_aetherDspModePolicy.update(restricted,
            m_audio->nr2External() ? ananPostProcessingMethod() : activeAetherDspMethod());
    if (action.kind == AetherDspModePolicy::ActionKind::Disable) {
        qCInfo(lcAudio).noquote()
            << "AetherDSP: disabling" << action.method
            << "because an audible slice uses CW or a digital mode";
        setAetherDspMethodEnabled(action.method, false);
    } else if (action.kind == AetherDspModePolicy::ActionKind::Enable) {
        qCInfo(lcAudio).noquote()
            << "AetherDSP: restoring" << action.method
            << "after all audible slices returned to compatible modes";
        setAetherDspMethodEnabled(action.method, true);
    }
}

void MainWindow::wirePooDooTiles()
{
    // ── PooDoo RX chain status tiles (Phase 0 chassis) ─────────────────────
    // RADIO tile = PC Audio enabled (standard SSB / remote_audio_rx stream).
    // DSP tile   = any client-side NR active (NR4 / DFNR / BNR).
    // SPEAK tile = AudioEngine output unmuted.
    if (auto* chain = m_appletPanel->clientChainApplet()) {
        // RADIO — driven by the existing PC Audio toggle in TitleBar.
        // Also forward to the strip's RX chain so its RADIO tile
        // tracks the same state.
        connect(m_titleBar, &TitleBar::pcAudioToggled, this,
                [this, chain](bool on) {
            chain->setRxPcAudioEnabled(on);
            updateTxAudioPathNotice();
        });

        // DSP — aggregate of every client-side NR module.  These are
        // mutually exclusive in AudioEngine::processRx (chained
        // if/else), so at most one is active at a time.  The tile
        // greens whenever any is on, and its label rotates to the
        // active module's short name (NR2 / RN2 / NR4 / DFNR / MNR /
        // BNR).  Shared state lives in a struct held by shared_ptr
        // captured into each lambda so lifetime ends with the last
        // connected slot.
        struct DspState {
            bool nr2{false}, rn2{false}, nr4{false},
                 dfnr{false}, mnr{false}, bnr{false}, nnr{false};
        };
        auto dspState = std::make_shared<DspState>();
        auto pushDsp = [this, chain, dspState]() {
            // Priority order picks the most "specific" / most-recent
            // module if more than one is somehow on at once.  Same
            // order as the audio-thread dispatcher so the displayed
            // label matches what's actually processing.
            QString label;
            if      (dspState->nnr)  label = "NNR";
            else if (dspState->bnr)  label = "BNR";
            else if (dspState->mnr)  label = "MNR";
            else if (dspState->dfnr) label = "DFNR";
            else if (dspState->nr4)  label = "NR4";
            else if (dspState->rn2)  label = "RN2";
            else if (dspState->nr2)  label = "NR2";
            const bool anyOn = !label.isEmpty();
            chain->setRxClientDspActive(anyOn, label);
        };
        connect(m_audio, &AudioEngine::nr2EnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->nr2 = on; pushDsp(); });
        connect(m_audio, &AudioEngine::rn2EnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->rn2 = on; pushDsp(); });
        connect(m_audio, &AudioEngine::nr4EnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->nr4 = on; pushDsp(); });
        connect(m_audio, &AudioEngine::dfnrEnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->dfnr = on; pushDsp(); });
        connect(m_audio, &AudioEngine::mnrEnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->mnr = on; pushDsp(); });
        connect(m_audio, &AudioEngine::nvAfxEnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->bnr = on; pushDsp(); });
        connect(m_audio, &AudioEngine::nnrEnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->nnr = on; pushDsp(); });

        // SPEAK — AudioEngine emits mutedChanged on every setMuted() flip.
        connect(m_audio, &AudioEngine::mutedChanged, this,
                [this, chain](bool muted) {
            chain->setRxOutputUnmuted(!muted);
        });

        // Seed initial state — settings and engine values are already
        // loaded by the time we reach this wiring code.  We pull from
        // the engine (not signals) so an already-on NR module shows up
        // immediately instead of waiting for the next toggle.
        const bool pcOn = AppSettings::instance()
            .value("PcAudioEnabled", "True").toString() == "True";
        chain->setRxPcAudioEnabled(pcOn);
        updateTxAudioPathNotice();
        dspState->nr2  = m_audio->nr2Enabled();
        dspState->rn2  = m_audio->rn2Enabled();
        dspState->nr4  = m_audio->nr4Enabled();
        dspState->dfnr = m_audio->dfnrEnabled();
        dspState->mnr  = m_audio->mnrEnabled();
        dspState->bnr  = m_audio->nvAfxEnabled();   // BNR == local AFX denoiser
        dspState->nnr  = m_audio->nnrEnabled();
        pushDsp();
        chain->setRxOutputUnmuted(!m_audio->isMuted());
    }
}

void MainWindow::wireDspApplets()
{
    // ── P/CW applet: mic meters + ALC meter + model ────────────────────────
    // Suppress radio CODEC meters when mic_selection=PC (they just show noise).
    // Client-side metering handles PC mic display below.
    // Compression gauge: full 20fps meter rate, gated on actual radio TX + PROC.
    {
        connect(&m_radioModel.meterModel(), &MeterModel::micMetersChanged,
                this, [this](float micLevel, float compLevel, float micPeak, float compPeak) {
            // Mic level: hardware mic uses radio meters, PC uses client-side.
            // The Level gauge shows what the S-meter's Level face shows
            // (MeterModel::transmitLevelFaceValue): MICPEAK where the radio
            // publishes no MIC meter (HL2).
            if (m_radioModel.transmitModel().micSelection() != "PC")
                m_appletPanel->phoneCwApplet()->updateMeters(
                    m_radioModel.meterModel().transmitLevelFaceValue(micLevel, micPeak),
                    compLevel, micPeak, 0.0f);

            // Compression has no useful meaning in RX; FLEX-8000 radios can
            // publish quiescent TX-chain meters there that look fully pegged.
            {
                const auto& tx = m_radioModel.transmitModel();
                const bool showCompression =
                    m_radioModel.isRadioTransmitting() && tx.speechProcessorEnable();
                m_appletPanel->phoneCwApplet()->updateCompression(
                    showCompression ? compPeak : 0.0f);
            }
        });
    }
    connect(&m_radioModel.meterModel(), &MeterModel::alcValueChanged,
            this, [this](float alc, const QString& unit) {
        // FLEX-8000 TX-chain meters can publish quiescent RX values near 0 dBFS.
        // Only show SW ALC while the radio interlock says RF is actually keyed.
        m_appletPanel->phoneCwApplet()->setAlcMeterUnit(unit);
        if (!unit.isEmpty() && m_radioModel.isRadioTransmitting()) {
            m_appletPanel->phoneCwApplet()->updateAlc(alc);
        } else {
            m_appletPanel->phoneCwApplet()->resetAlc();
        }
    });
    // ALC applied-gain row. Shown only with a sample (hasAlcGainValue()), since
    // a cleared meter would otherwise read as "holding at unity". Whether the
    // row exists is decided on the meter-definition signals, not on values:
    // alcGain fires only while keyed, and Hl2Backend defines meters after
    // capabilities are published. clear() emits only metersCleared. Idempotent.
    {
        auto syncAlcGainRow = [this] {
            m_appletPanel->phoneCwApplet()->setHasAlcGainMeter(
                m_radioModel.meterModel().hasAlcGainMeter());
        };
        connect(&m_radioModel.meterModel(), &MeterModel::meterDefinitionChanged,
                this, [syncAlcGainRow](int) { syncAlcGainRow(); });
        connect(&m_radioModel.meterModel(), &MeterModel::meterRemoved,
                this, [syncAlcGainRow](int) { syncAlcGainRow(); });
        connect(&m_radioModel.meterModel(), &MeterModel::metersCleared,
                this, syncAlcGainRow);
    }
    connect(&m_radioModel.meterModel(), &MeterModel::alcGainChanged,
            this, [this](float gainDb) {
        // READING ONLY. Presence is settled above, on the definition signals.
        const bool live = m_radioModel.isRadioTransmitting()
                       && m_radioModel.meterModel().hasAlcGainValue();
        if (live) {
            m_appletPanel->phoneCwApplet()->updateAlcGain(gainDb);
        } else {
            m_appletPanel->phoneCwApplet()->resetAlcGain();
        }
    });
    // Client-side PC mic metering — radio CODEC meters only see hardware mics.
    // Apply VU-style ballistics: fast attack, slow decay (~20 dB/sec).
    {
        auto heldLevel = std::make_shared<float>(-150.0f);
        auto heldPeak  = std::make_shared<float>(-150.0f);
        connect(m_audio, &AudioEngine::pcMicLevelChanged,
                this, [this, heldLevel, heldPeak](float peakDb, float avgDb) {
            if (m_radioModel.transmitModel().micSelection() != "PC" && !m_audio->isRadeMode()) return;
            // ANAN-100D port: the radio's own microphone is selected -- its
            // meter comes from codecMicLevelChanged below.
            // (PC and CDC both run through the TX voice chain since 06.11;
            // only TEST is metered by the backend.)
            if (AppSettings::instance().value("AnanMicSource", 0).toInt() == 2) return;
            constexpr float kDecayPerUpdate = 1.0f;  // ~20 dB/sec at 20 updates/sec
            // Level: fast attack, slow decay
            if (avgDb > *heldLevel)
                *heldLevel = avgDb;
            else
                *heldLevel = qMax(avgDb, *heldLevel - kDecayPerUpdate);
            // Peak: fast attack, slower decay
            if (peakDb > *heldPeak)
                *heldPeak = peakDb;
            else
                *heldPeak = qMax(*heldLevel, *heldPeak - kDecayPerUpdate * 0.5f);
            m_appletPanel->phoneCwApplet()->updateMeters(*heldLevel, 0.0f, *heldPeak, 0.0f);
        });
    }
    // ANAN-100D port: meter for the radio's own microphone, same ballistics.
    {
        auto heldLevel = std::make_shared<float>(-150.0f);
        auto heldPeak  = std::make_shared<float>(-150.0f);
        connect(&m_radioModel, &RadioModel::codecMicLevelChanged,
                this, [this, heldLevel, heldPeak](float peakDb, float avgDb) {
            if (AppSettings::instance().value("AnanMicSource", 0).toInt() != 2) return;
            constexpr float kDecayPerUpdate = 1.0f;
            *heldLevel = avgDb > *heldLevel ? avgDb : qMax(avgDb, *heldLevel - kDecayPerUpdate);
            *heldPeak = peakDb > *heldPeak ? peakDb : qMax(*heldLevel, *heldPeak - kDecayPerUpdate * 0.5f);
            m_appletPanel->phoneCwApplet()->updateMeters(*heldLevel, 0.0f, *heldPeak, 0.0f);
        });
    }
    m_appletPanel->phoneCwApplet()->setTransmitModel(&m_radioModel.transmitModel());
    const auto updatePhoneCwReceiveOnly = [this](bool connected, const RadioCapabilities& caps) {
        m_appletPanel->phoneCwApplet()->setTransmitControlsAvailable(!connected || caps.canTransmit);
    };
    connect(&m_radioModel, &RadioModel::capabilitiesChanged, this, updatePhoneCwReceiveOnly);
    updatePhoneCwReceiveOnly(m_radioModel.isConnected(), m_radioModel.backendCapabilities());





    // ── EQ applet: graphic equalizer ─────────────────────────────────────────
    m_appletPanel->eqApplet()->setEqualizerModel(&m_radioModel.equalizerModel());

    // ── Client EQ applets: dedicated TX and RX tiles (Phase 7.1) ───────────
    m_appletPanel->clientEqTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientEqRxApplet()->setAudioEngine(m_audio);

    auto wireEqEditOpen = [this](ClientEqApplet* applet) {
        connect(applet, &ClientEqApplet::editRequested, this,
                [this](ClientEqApplet::Path path) {
            if (path == ClientEqApplet::Path::Tx && showTxAudioPathErrorIfBlocked()) return;
            ensureClientEqEditor()->showForPath(path);
        });
    };
    wireEqEditOpen(m_appletPanel->clientEqTxApplet());
    wireEqEditOpen(m_appletPanel->clientEqRxApplet());

    // Push TX low/high filter cutoffs to the EQ canvases as dashed yellow
    // guide lines.  Subscribes to the *dedicated* txFilterCutoffChanged
    // signal — NOT the omnibus phoneStateChanged which fires on every
    // VOX/CW/mic-boost/dexp/etc. transmit-status update and would
    // cascade unnecessary repaints into the audio path during TX.
    auto pushTxFilterCutoffsToEq = [this](int lo, int hi) {
        if (m_appletPanel && m_appletPanel->clientEqTxApplet())
            m_appletPanel->clientEqTxApplet()->setTxFilterCutoffs(lo, hi);
        if (m_clientEqEditor)
            m_clientEqEditor->setTxFilterCutoffs(lo, hi);
        if (m_aetherialStrip)
            m_aetherialStrip->setTxFilterCutoffs(lo, hi);
    };
    {
        const auto& tx = m_radioModel.transmitModel();
        pushTxFilterCutoffsToEq(tx.txFilterLow(), tx.txFilterHigh());
    }
    connect(&m_radioModel.transmitModel(), &TransmitModel::txFilterCutoffChanged,
            this, pushTxFilterCutoffsToEq);

    // RX filter passband guide lines on the RX EQ canvas — fed by the
    // currently-active RX slice.  filterLow / filterHigh on a slice are
    // *offsets* (e.g. -3000..0 for LSB, 0..3000 for USB, -3000..3000 for
    // AM); the EQ canvas plots in absolute audio-frequency.  Convert:
    //   audio_high = max(|lo|, |hi|)
    //   audio_low  = (lo and hi same sign / one zero) ? min(|lo|, |hi|) : 0
    // Then push to the docked RX-bound applet + floating editor (if open).
    // setActiveSlice() and SliceModel::filterChanged both call this lambda
    // so the guides track both slice swaps and live filter drags.
    pushRxFilterCutoffsToEq();
    connect(&m_radioModel, &RadioModel::sliceAdded, this, [this](SliceModel* s) {
        if (!s) return;
        connect(s, &SliceModel::filterChanged, this,
                [this, s](int /*lo*/, int /*hi*/) {
            if (s->sliceId() == m_activeSliceId)
                pushRxFilterCutoffsToEq();
        });
    });

    // ── Client Compressor applets: TX (#1661) + RX (Phase 7.3) ─────────────
    m_appletPanel->clientCompTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientCompRxApplet()->setAudioEngine(m_audio);
    connect(m_appletPanel->clientCompTxApplet(), &ClientCompApplet::editRequested,
            this, [this]() {
        if (!showTxAudioPathErrorIfBlocked()) ensureClientCompEditor()->showForTx();
    });
    connect(m_appletPanel->clientCompRxApplet(), &ClientCompApplet::editRequested,
            this, [this]() { ensureClientCompEditor()->showForRx(); });

    // ── Client Gate applets: TX (#1661 Phase 2) + RX (Phase 7.2) ───────────
    m_appletPanel->clientGateTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientGateRxApplet()->setAudioEngine(m_audio);
    connect(m_appletPanel->clientGateTxApplet(), &ClientGateApplet::editRequested,
            this, [this]() {
        if (!showTxAudioPathErrorIfBlocked()) ensureClientGateEditor()->showForTx();
    });
    connect(m_appletPanel->clientGateRxApplet(), &ClientGateApplet::editRequested,
            this, [this]() { ensureClientGateEditor()->showForRx(); });

    // ── Client De-esser applet: TX sidechain-filtered dynamics (#1661 Phase 3) ─
    m_appletPanel->clientDeEssApplet()->setAudioEngine(m_audio);

    // ── Client Tube applets: TX (#1661) + RX (Phase 7.4) ───────────────────
    m_appletPanel->clientTubeTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientTubeRxApplet()->setAudioEngine(m_audio);
    connect(m_appletPanel->clientTubeTxApplet(), &ClientTubeApplet::editRequested,
            this, [this]() {
        if (!showTxAudioPathErrorIfBlocked()) ensureClientTubeEditor()->showForTx();
    });
    connect(m_appletPanel->clientTubeRxApplet(), &ClientTubeApplet::editRequested,
            this, [this]() { ensureClientTubeEditor()->showForRx(); });

    // ── Client Reverb applet: TX reverb (Freeverb) ─
    m_appletPanel->clientReverbApplet()->setAudioEngine(m_audio);

    // ── RX-side AetherDSP applet — same controls as the Settings menu
    // dialog, embedded as a docked tile in PooDoo Audio (RX).  Parameter
    // changes route through the same per-signal wiring used by the dialog,
    // factored into wireAetherDspWidget() to keep dialog + applet in sync.
    if (auto* a = m_appletPanel->clientRxDspApplet()) {
        a->setAudioEngine(m_audio);
        if (auto* w = a->widget())
            wireAetherDspWidget(w);
    }

    // ── Client PUDU applets: TX (#1661 Phase 5) + RX (Phase 7.5) ───────────
    m_appletPanel->clientPuduTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientPuduRxApplet()->setAudioEngine(m_audio);
    connect(m_appletPanel->clientPuduTxApplet(), &ClientPuduApplet::editRequested,
            this, [this]() {
        if (!showTxAudioPathErrorIfBlocked()) ensureClientPuduEditor()->showForTx();
    });
    connect(m_appletPanel->clientPuduRxApplet(), &ClientPuduApplet::editRequested,
            this, [this]() { ensureClientPuduEditor()->showForRx(); });

    // ── TX signal-chain applet (#1661) ──────────────────────────────────────
    // Visual strip showing MIC → stages → TX with per-stage bypass +
    // drag-drop reorder.  Clicking a stage opens its floating editor.
    //
    // TX-stage applet visibility is independent of bypass state — the
    // chain widget click and the editor Bypass buttons toggle the DSP
    // and refresh the applet's bypass indicator, but do not show or
    // hide the tile.  Users control applet visibility via the applet
    // header ✕ and toolbar toggles (persisted as Applet_<ID>).
    m_appletPanel->clientChainApplet()->setAudioEngine(m_audio);
    // PooDoo TX/RX tab → AppletPanel side filter.  Hides the inactive
    // side's per-stage applet tiles whenever the user flips the chain
    // tab.  Seed the initial side from the saved tab so the first
    // paint is correct.
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::chainModeChanged,
            this, [this](ClientChainApplet::ChainMode mode) {
        if (!m_appletPanel) return;
        m_appletPanel->setPooDooActiveSide(
            mode == ClientChainApplet::ChainMode::Tx
                ? AppletPanel::PooDooSide::Tx
                : AppletPanel::PooDooSide::Rx);
    });
    // Side-filter seed is moved further down — must run AFTER
    // setTxDspChainOrder, otherwise that helper's insertChildWidget
    // calls re-show every TX container we just hid.

    // Seed the PooDoo MIC-ready + TX-pulse indicators from current
    // state — subsequent changes flow through the TransmitModel
    // signal connections above (micStateChanged + moxChanged).
    {
        const auto& tx = m_radioModel.transmitModel();
        const bool ready = (tx.micSelection() == "PC") && !tx.daxOn();
        m_appletPanel->clientChainApplet()->setMicInputReady(ready);
        m_appletPanel->clientChainApplet()->setTxActive(
            ready && tx.isTransmitting());
        if (m_aetherialStrip) {
            m_aetherialStrip->setMicInputReady(ready);
            m_aetherialStrip->setTxActive(ready && tx.isTransmitting());
        }
    }
    // Pulse the TX endpoint red when we're transmitting AND PooDoo
    // is actually in the signal path (MIC=PC and DAX off).  Otherwise
    // the pulse would lie about what's being processed.
    connect(&m_radioModel.transmitModel(), &TransmitModel::moxChanged,
            this, [this](bool txActive) {
        if (!m_appletPanel || !m_appletPanel->clientChainApplet()) return;
        const auto& tx = m_radioModel.transmitModel();
        const bool ready = (tx.micSelection() == "PC") && !tx.daxOn();
        m_appletPanel->clientChainApplet()->setTxActive(ready && txActive);
        if (m_aetherialStrip)
            m_aetherialStrip->setTxActive(ready && txActive);
    });

    // ── PUDU monitor wiring ─────────────────────────────────────
    auto* chainApplet = m_appletPanel->clientChainApplet();
    chainApplet->setMonitorHasRecording(m_finalMonitor->hasRecording());

    // Easter-egg nub on the chain applet → toggle the Aetherial Audio
    // Channel Strip.  Stubbed in step 1 of the strip plan (#2301);
    // step 4 lazy-creates the strip window and toggles visibility.
    connect(chainApplet, &ClientChainApplet::aetherialStripToggleRequested,
            this, &MainWindow::toggleAetherialStrip);

    // User-click → start/stop based on current monitor state.  The
    // monitor's own signals drive the button visuals back.
    connect(chainApplet, &ClientChainApplet::monitorRecordClicked,
            this, [this]() {
        if (m_finalMonitor->isRecording()) {
            m_finalMonitor->stopRecording();
        } else {
            // Don't record while playing — button shouldn't be
            // enabled in that state, but guard anyway.
            if (m_finalMonitor->isPlaying()) m_finalMonitor->stopPlayback();
            m_finalMonitor->startRecording();
        }
    });
    connect(chainApplet, &ClientChainApplet::monitorPlayClicked,
            this, [this]() {
        if (m_finalMonitor->isPlaying()) {
            m_finalMonitor->stopPlayback();
        } else {
            m_finalMonitor->startPlayback();
        }
    });

    // Monitor state → UI updates.  RX audio gating is handled
    // separately via the muteRxRequested wiring above.  State is
    // forwarded both to the docked ClientChainApplet AND to the
    // AetherialAudioStrip's mirrored buttons (when the strip exists).
    connect(m_finalMonitor, &ClientPuduMonitor::recordingStarted,
            this, [this]() {
        if (m_appletPanel && m_appletPanel->clientChainApplet())
            m_appletPanel->clientChainApplet()->setMonitorRecording(true);
        if (m_aetherialStrip)
            m_aetherialStrip->setMonitorRecording(true);
    });
    connect(m_finalMonitor, &ClientPuduMonitor::recordingStopped,
            this, [this](int /*durationMs*/) {
        if (m_appletPanel && m_appletPanel->clientChainApplet()) {
            auto* a = m_appletPanel->clientChainApplet();
            a->setMonitorRecording(false);
            a->setMonitorHasRecording(true);
        }
        if (m_aetherialStrip) {
            m_aetherialStrip->setMonitorRecording(false);
            m_aetherialStrip->setMonitorHasRecording(true);
        }
        // Auto-start playback — the mute stays installed across the
        // transition because the monitor only emits muteRxRequested
        // (false) at stopPlayback().
        m_finalMonitor->startPlayback();
    });
    connect(m_finalMonitor, &ClientPuduMonitor::playbackStarted,
            this, [this]() {
        if (m_appletPanel && m_appletPanel->clientChainApplet())
            m_appletPanel->clientChainApplet()->setMonitorPlaying(true);
        if (m_aetherialStrip)
            m_aetherialStrip->setMonitorPlaying(true);
    });
    connect(m_finalMonitor, &ClientPuduMonitor::playbackStopped,
            this, [this]() {
        if (m_appletPanel && m_appletPanel->clientChainApplet())
            m_appletPanel->clientChainApplet()->setMonitorPlaying(false);
        if (m_aetherialStrip)
            m_aetherialStrip->setMonitorPlaying(false);
    });
    // TX chain applet visibility is independent of bypass state — the
    // user controls show/hide via the applet header ✕ and toolbar
    // toggles, persisted via Applet_<ID>.  Bypassing a stage just
    // shows the applet as bypassed; it doesn't hide the tile.

    // Initial applet-stack order mirrors the persisted chain order
    // for both sides, and stays in sync on every subsequent drag-
    // reorder.
    if (m_audio) {
        m_appletPanel->setTxDspChainOrder(m_audio->txChainStages());
        m_appletPanel->setRxDspChainOrder(m_audio->rxChainStages());
    }
    auto reapplyPooDooSide = [this]() {
        if (!m_appletPanel) return;
        const QString savedTab = AppSettings::instance()
            .value("PooDooAudioActiveTab", "TX").toString();
        m_appletPanel->setPooDooActiveSide(
            savedTab == "RX" ? AppletPanel::PooDooSide::Rx
                             : AppletPanel::PooDooSide::Tx);
    };
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::chainReordered,
            this, [this, reapplyPooDooSide]() {
        if (!m_appletPanel || !m_audio) return;
        m_appletPanel->setTxDspChainOrder(m_audio->txChainStages());
        // setTxDspChainOrder re-shows every reinserted child, so re-
        // apply the side filter to keep the inactive side hidden.
        reapplyPooDooSide();
    });
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::rxChainReordered,
            this, [this, reapplyPooDooSide]() {
        if (!m_appletPanel || !m_audio) return;
        m_appletPanel->setRxDspChainOrder(m_audio->rxChainStages());
        reapplyPooDooSide();
    });

    // PooDoo TX/RX side-filter seed — runs AFTER setTxDspChainOrder
    // because that helper's insertChildWidget unconditionally shows
    // each child it reinserts, undoing any earlier hide.  Putting the
    // seed here ensures the inactive side stays hidden on first paint.
    {
        const QString savedTab = AppSettings::instance()
            .value("PooDooAudioActiveTab", "TX").toString();
        m_appletPanel->setPooDooActiveSide(
            savedTab == "RX" ? AppletPanel::PooDooSide::Rx
                             : AppletPanel::PooDooSide::Tx);
    }

    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::stageEnabledChanged,
            this, &MainWindow::onTxChainStageEnabledChanged);

    // ── RX chain edit + bypass ──────────────────────────────────────────────
    // Phase 1 routes RX EQ double-clicks to the existing ClientEqEditor in
    // RX path mode.  Click-bypass lands on the engine via the chain widget
    // itself; we just refresh the CEQ applet's Enable toggle here so it
    // stays in sync.
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::rxEditRequested,
            this, [this](AudioEngine::RxChainStage stage) {
        switch (stage) {
            case AudioEngine::RxChainStage::Eq:
                ensureClientEqEditor()->showForPath(ClientEqApplet::Path::Rx);
                break;
            case AudioEngine::RxChainStage::Gate:
                ensureClientGateEditor()->showForRx();
                break;
            case AudioEngine::RxChainStage::Comp:
                ensureClientCompEditor()->showForRx();
                break;
            case AudioEngine::RxChainStage::Tube:
                ensureClientTubeEditor()->showForRx();
                break;
            case AudioEngine::RxChainStage::Pudu:
                ensureClientPuduEditor()->showForRx();
                break;
            default:
                break;
        }
    });
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::rxStageEnabledChanged,
            this, [this](AudioEngine::RxChainStage stage, bool /*enabled*/) {
        // Keep the shared per-stage applet's Enable toggle in lock-
        // step with the click-bypass that just fired on the chain
        // widget.  Each stage routes to its own applet refresh.
        if (!m_appletPanel) return;
        switch (stage) {
            case AudioEngine::RxChainStage::Eq:
                if (m_appletPanel->clientEqRxApplet())
                    m_appletPanel->clientEqRxApplet()->refreshEnableFromEngine();
                break;
            case AudioEngine::RxChainStage::Gate:
                if (m_appletPanel->clientGateRxApplet())
                    m_appletPanel->clientGateRxApplet()->refreshEnableFromEngine();
                break;
            case AudioEngine::RxChainStage::Comp:
                if (m_appletPanel->clientCompRxApplet())
                    m_appletPanel->clientCompRxApplet()->refreshEnableFromEngine();
                break;
            case AudioEngine::RxChainStage::Tube:
                if (m_appletPanel->clientTubeRxApplet())
                    m_appletPanel->clientTubeRxApplet()->refreshEnableFromEngine();
                break;
            case AudioEngine::RxChainStage::Pudu:
                if (m_appletPanel->clientPuduRxApplet())
                    m_appletPanel->clientPuduRxApplet()->refreshEnableFromEngine();
                break;
            default:
                break;
        }
    });
}

// Speech processor on a host-modulating backend. PROC and NOR/DX/DX+ map to
// `transmit set speech_processor_*`, which reach nothing without a Flex command
// plane, so they drive the ClientComp already in AudioEngine's TX chain — the
// same instance the Aetherial strip edits, so two compressors never run in
// series. Level presets overwrite the strip's settings, so they apply only on
// a level change or off→on.

bool MainWindow::hostModulatesTxAudio() const
{
    // The same capability pair MainWindow_Session.cpp tests before opening the
    // mic: a backend host-modulates only if it says so AND may transmit.
    const RadioCapabilities caps = m_radioModel.backendCapabilities();
    return caps.hostModulates && caps.canTransmit;
}

void MainWindow::applySpeechProcessorToClientComp(bool operatorIntent)
{
    if (!m_audio || !hostModulatesTxAudio())
        return;
    ClientComp* comp = m_audio->clientCompTx();
    if (!comp)
        return;

    const auto& tx = m_radioModel.transmitModel();
    const bool on = tx.speechProcessorEnable();

    // ANAN fork build 719 (operator, 25.11: "a simple compressor for the
    // microphone, used by PROC, so that this is separated too; the settings
    // as close as possible to the three presets"). Replaces builds 688 / 695 /
    // 696, which wrote the NOR / DX / DX+ presets into the Aether strip's
    // compressor: PROC has its own compressor now, in the backend
    // (Hl2Backend::applyCodecProcessor / procCompProcess, the same presets),
    // and the two are mutually exclusive, as the equalisers (build 717):
    //  - PROC switched on by the operator switches the Aether compressor off;
    //  - the Aether compressor switched on switches PROC off (the mirror in
    //    wireHostModulatedVoiceChain);
    //  - the slider and PROC off touch nothing here.
    // Only the operator's PROC moves act (speechProcessorCommandIssued); the
    // observe call (micStateChanged, also emitted by the mirror) does nothing.
    if (!operatorIntent)
        return;
    const bool switchedOn = on && !m_lastAppliedProcEnable;
    m_lastAppliedProcEnable = on;
    m_lastAppliedProcLevel = qBound(0, tx.speechProcessorLevel(), 2);
    if (!switchedOn || !comp->isEnabled())
        return;
    comp->setEnabled(false);
    m_audio->saveClientCompSettings();
    m_aetherCompLastOn = 0;
    qInfo() << "PROC on: the Aether TX compressor switched off";
    // The strip's own compressor tile reads its checked state from the engine,
    // so it has to be told the object underneath it changed.
    if (m_appletPanel) {
        if (m_appletPanel->clientCompTxApplet())
            m_appletPanel->clientCompTxApplet()->refreshEnableFromEngine();
        if (m_appletPanel->clientChainApplet())
            m_appletPanel->clientChainApplet()->refreshFromEngine();
    }
}

// ANAN fork build 717 (operator, 25.11: "we have to separate the equalisers
// of the applet and of AetherRX / AetherTX. They must work mutually
// exclusively: one goes on, the other goes off. Make a simple equaliser on
// WDSP at the sliders' frequencies, with the right Q for the number of
// sliders, for receive and transmit independently"). Replaces builds
// 687 / 593: the applet's 8-band graphic EQ no longer writes the shared
// ClientEq of the Aether strip; it is WDSP's own equaliser -- the RXA one in
// every receiver, the TXA one in the transmitter (GraphicEqCurve.h: a peaking
// band per slider, Q of the sliders' one-octave spacing, the sliders' values
// met at their centres). The backend takes it from AnanUiState
// (Hl2Backend, next to the BIN push). The name stays for its callers.
//
// Mutually exclusive with the Aether EQ of the same side: the graphic EQ
// switched on switches that side's ClientEq off here; the ClientEq switched
// on (its editors and tiles give no signal) switches the graphic EQ off in
// syncGraphicEqFromClientEq() below.
void MainWindow::applyGraphicEqToClientEq(bool transmit)
{
    // Flex is excluded: there the sliders reach the radio's own 8-band
    // hardware EQ through the command plane.
    if (!m_audio || m_radioModel.usesFlexCommandPlane())
        return;
    const auto& model = m_radioModel.equalizerModel();
    const bool enabled = transmit ? model.txEnabled() : model.rxEnabled();
    std::array<double, 8> db{};
    for (int i = 0; i < EqualizerModel::BandCount; ++i) {
        const auto band = static_cast<EqualizerModel::Band>(i);
        db[static_cast<std::size_t>(i)] =
            static_cast<double>(transmit ? model.txBand(band) : model.rxBand(band));
    }
    anan::AnanUiState::instance().setGraphicEq(transmit ? 1 : 0, enabled, db);

    const int side = transmit ? 1 : 0;
    if (enabled) {
        ClientEq* eq = transmit ? m_audio->clientEqTx() : m_audio->clientEqRx();
        if (eq && eq->isEnabled()) {
            eq->setEnabled(false);
            m_audio->saveClientEqSettings();
            qInfo() << "Graphic EQ" << (transmit ? "TX" : "RX")
                    << "on: the Aether EQ of that side switched off";
            if (m_appletPanel) {
                auto* tile = transmit ? m_appletPanel->clientEqTxApplet()
                                      : m_appletPanel->clientEqRxApplet();
                if (tile)
                    tile->refreshEnableFromEngine();
                if (m_appletPanel->clientChainApplet())
                    m_appletPanel->clientChainApplet()->refreshFromEngine();
            }
        }
        m_aetherEqLastOn[side] = 0;
    }
}

// Build 717: the Aether EQ switched on by its own controls switches the
// graphic EQ of that side off (four times a second; the ClientEq gives no
// signal). Replaces build 687's mirroring of its gains and enable. At the
// first look, both on (a state left by 687's synchronisation) keeps the
// Aether EQ, whose settings were the ones actually heard.
void MainWindow::syncGraphicEqFromClientEq()
{
    if (!m_audio || m_radioModel.usesFlexCommandPlane() || m_graphicEqFromRadio)
        return;
    auto& model = m_radioModel.equalizerModel();
    for (int side = 0; side < 2; ++side) {
        ClientEq* eq = side ? m_audio->clientEqTx() : m_audio->clientEqRx();
        if (!eq)
            continue;
        const int on = eq->isEnabled() ? 1 : 0;
        const int last = m_aetherEqLastOn[side];
        m_aetherEqLastOn[side] = on;
        if (!on || last == 1)
            continue;   // off, or no rising edge
        const bool graphicOn = side ? model.txEnabled() : model.rxEnabled();
        if (!graphicOn)
            continue;
        qInfo() << "Aether EQ" << (side ? "TX" : "RX")
                << "on: the graphic EQ of that side switched off";
        if (side)
            model.setTxEnabled(false);
        else
            model.setRxEnabled(false);
    }
}

void MainWindow::wireHostModulatedVoiceChain()
{
    // Build 687: the Aether EQ -> the graphic EQ, four times a second.
    // Build 717: only the mutual exclusion (syncGraphicEqFromClientEq).
    {
        auto* eqSyncTimer = new QTimer(this);
        eqSyncTimer->setInterval(250);
        connect(eqSyncTimer, &QTimer::timeout, this, [this] { syncGraphicEqFromClientEq(); });
        eqSyncTimer->start();
    }

    // The persisted graphic EQ comes back BEFORE the ClientEq binding below, so
    // restoring writes nothing into the shared objects; the connect-edge re-push
    // does, and only on a host-modulating backend. A restored layout licenses
    // that re-push and is not ownership, so a Flex connected first unwinds nothing.
    {
        const EqualizerModel::State restored = EqualizerModel::loadPersistedState();
        m_radioModel.equalizerModel().restoreState(restored);
        m_restoredGraphicEqPending = restoredGraphicEqHoldsLayout(
            restored.anyBandNonZero(), restored.anyEnabled());
    }

    // The graphic EQ. Bound on BOTH state signals rather than on a capability
    // check at wiring time, because the capability is not known until a backend
    // attaches; applyGraphicEqToClientEq() re-checks on every call.
    connect(&m_radioModel.equalizerModel(), &EqualizerModel::txStateChanged,
            this, [this] {
        if (!m_graphicEqReloading)
            applyGraphicEqToClientEq(true);
    });
    connect(&m_radioModel.equalizerModel(), &EqualizerModel::rxStateChanged,
            this, [this] {
        if (!m_graphicEqReloading)
            applyGraphicEqToClientEq(false);
    });

    // Operator -> settings, in the EqApplet object and never in the strip's
    // ClientEq keys. Not on a Flex, whose EQ the radio keeps, nor while the
    // model still holds a Flex's values.
    connect(&m_radioModel.equalizerModel(), &EqualizerModel::operatorEdited,
            this, [this] {
        if (graphicEqPersistAllowed(m_radioModel.isConnected(),
                                    hostModulatesTxAudio(),
                                    m_graphicEqFromRadio)) {
            EqualizerModel::savePersistedState(m_radioModel.equalizerModel().state());
        }
    });

    // Operator -> DSP.
    //
    // TWO signals, and the split is load-bearing. speechProcessorCommandIssued
    // is the operator actually moving PROC, and only that is allowed to write
    // the NOR/DX/DX+ preset over the shared compressor's settings.
    // micStateChanged is any state movement at all — including the mirror below
    // reporting a strip-originated enable — and may only sync the enable flag.
    connect(&m_radioModel.transmitModel(),
            &TransmitModel::speechProcessorCommandIssued, this,
            [this] { applySpeechProcessorToClientComp(true); });
    connect(&m_radioModel.transmitModel(), &TransmitModel::micStateChanged,
            this, [this] { applySpeechProcessorToClientComp(false); });

    // ── Connection edges ────────────────────────────────────────────────────
    connect(&m_radioModel, &RadioModel::connectionStateChanged,
            this, [this](bool connected) {
        const bool hostModulates = hostModulatesTxAudio();

        if (connected && hostModulates) {
            // After a Flex the model holds the radio's EQ: the saved layout comes
            // back silently first, and licenses the re-push as at startup.
            if (graphicEqReloadRequired(connected, hostModulates,
                                        m_graphicEqFromRadio)) {
                const EqualizerModel::State saved = EqualizerModel::loadPersistedState();
                m_graphicEqReloading = true;
                m_radioModel.equalizerModel().restoreState(saved);
                m_graphicEqReloading = false;
                m_restoredGraphicEqPending = restoredGraphicEqHoldsLayout(
                    saved.anyBandNonZero(), saved.anyEnabled());
                m_graphicEqFromRadio = false;
            }
            // Re-push on connect, since the capability was unknown before. Only
            // what the operator set or what was restored
            // (hostVoiceChainRepushAllowed; core/HostVoiceChainPolicy.h): an
            // unconditional push would overwrite the saved strip with defaults.
            const bool restoredEq = m_restoredGraphicEqPending;
            m_restoredGraphicEqPending = false;
            // PROC does not persist: a restored graphic EQ alone must leave the
            // strip's compressor as the operator saved it.
            if (hostVoiceChainRepushAllowed(connected, hostModulates,
                                            m_hostVoiceChainOwned)) {
                // The cache is reset so the preset re-applies.
                m_lastAppliedProcLevel = -1;
                m_lastAppliedProcEnable = false;
                applySpeechProcessorToClientComp(true);
            }
            if (hostVoiceChainRepushAllowed(connected, hostModulates,
                                            m_hostVoiceChainOwned || restoredEq)) {
                applyGraphicEqToClientEq(true);
                applyGraphicEqToClientEq(false);
                // Sliders moved while disconnected are saved once they apply.
                EqualizerModel::savePersistedState(
                    m_radioModel.equalizerModel().state());
            }
            m_hostVoiceChainTimer.start();
            return;
        }

        m_hostVoiceChainTimer.stop();

        // A Flex writes its own EQ into the model, so the layout restored from
        // settings is no longer what the model holds: it must not be re-pushed.
        if (connected && m_radioModel.usesFlexCommandPlane()) {
            m_restoredGraphicEqPending = false;
            m_graphicEqFromRadio = true;
        }

        // Unwind on a family swap: after host-modulated EQ/PROC on an HL2, a
        // later Flex session's applies all return early, but ClientEq/ClientComp
        // stay ENABLED, double-equalizing with the radio's hardware EQ. Bounded
        // by ownership (m_hostVoiceChainOwned) so a plain Flex session never
        // loses the operator's strip. Bypass, not clear, so settings survive.
        // Fires once per HL2→Flex transition; a deliberate re-enable mid-Flex
        // session is left alone.
        if (m_audio
            && hostVoiceChainUnwindRequired(connected, hostModulates,
                                            m_radioModel.usesFlexCommandPlane(),
                                            m_hostVoiceChainOwned)) {
            if (auto* eqTx = m_audio->clientEqTx())   eqTx->setEnabled(false);
            if (auto* eqRx = m_audio->clientEqRx())   eqRx->setEnabled(false);
            if (auto* comp = m_audio->clientCompTx()) comp->setEnabled(false);
            // Out of circuit and no longer ours: a second Flex connect must not
            // disable them again after the operator has switched them back on.
            m_hostVoiceChainOwned = false;
            if (m_appletPanel) {
                if (auto* t = m_appletPanel->clientEqTxApplet())   t->refreshEnableFromEngine();
                if (auto* t = m_appletPanel->clientEqRxApplet())   t->refreshEnableFromEngine();
                if (auto* t = m_appletPanel->clientCompTxApplet()) t->refreshEnableFromEngine();
            }
        }
    });

    // DSP -> meter, and DSP -> operator.
    //
    // ClientComp has no change notification — every other consumer polls it on a
    // timer (ClientCompApplet::tickMeter) — so this does too, at the same 20 Hz
    // the compression gauge is fed at elsewhere. Started and stopped on the
    // connection edge above rather than running for the process lifetime.
    m_hostVoiceChainTimer.setInterval(50);
    connect(&m_hostVoiceChainTimer, &QTimer::timeout, this, [this] {
        if (!m_audio || !hostModulatesTxAudio())
            return;
        ClientComp* comp = m_audio->clientCompTx();
        if (!comp)
            return;

        // TX:COMPPEAK wants a POSITIVE amount of compression in dB;
        // ClientComp reports gain reduction as a value <= 0. Negate, do not
        // abs(): a positive gainReductionDb would be a bug upstream and
        // abs() would render it as heavy compression.
        // Build 719: the Aether compressor's while it is on; while PROC is on
        // and the radio transmits, the backend publishes PROC's compressor
        // (Hl2Backend::procCompProcess); otherwise nothing is compressed.
        auto& tx = m_radioModel.transmitModel();
        const bool aetherOn = comp->isEnabled();
        if (aetherOn || !tx.speechProcessorEnable() || !tx.isTransmitting()) {
            const float reduction = aetherOn ? -comp->gainReductionDb() : 0.0f;
            m_radioModel.meterModel().updateValueByName(
                QStringLiteral("TX"), QStringLiteral("COMPPEAK"),
                qBound(0.0f, reduction, 25.0f));
        }

        // Build 719: the Aether compressor switched on by its own controls
        // switches PROC off (mutually exclusive, see
        // applySpeechProcessorToClientComp). applySpeechProcessorState()
        // emits micStateChanged but NOT speechProcessorCommandIssued, so this
        // is never mistaken for the operator moving PROC; the backend hears it
        // (adoptSpeechProcessorFromChain -> controlRequested) and switches its
        // PROC compressor and the TXA compressor's PROC gain off. At the first
        // look, both on keeps the Aether compressor. Replaces build 696's
        // "PROC on, compressor off -> PROC off": that is PROC's normal state now.
        {
            const int last = m_aetherCompLastOn;
            m_aetherCompLastOn = aetherOn ? 1 : 0;
            if (aetherOn && last != 1 && tx.speechProcessorEnable()) {
                qInfo() << "Aether TX compressor on: PROC switched off";
                tx.adoptSpeechProcessorFromChain(false, tx.speechProcessorLevel());
                m_lastAppliedProcEnable = false;
            }
        }
    });
}

} // namespace AetherSDR
