// ANAN fork build 380/381: the FTX mode (FT8 / FT4 / WSPR; JS8 to come).
// Entering and leaving the mode: what MicronSDR's ftEnter() / ftExit() do
// (micronsdr.cpp), for this program. The window of the mode and its decoders
// are MicronSDR's own files (src/ftx: FtPanel, FtUnit, FtWaterfall, FtTx,
// FtNet, PskReporter; libftx.dll). See core/ftx/FtxState.h, gui/FtxView.h.
#include "MainWindow.h"

#include "FtxView.h"
#include "PanadapterApplet.h"
#include "PanadapterStack.h"
#include "Theme.h"
#include "core/PcmFrame.h"
#include "core/fdv/FdvEngine.h"   // HalfbandResampler: 24 -> 48 kHz
#include "core/ftx/FtxState.h"
#include "ftx/FtPanel.h"
#include "ftx/FtUnit.h"
#include "ftx/FtWaterfall.h"
#include "models/PanadapterModel.h"
#include "models/SliceModel.h"

#include <QMessageBox>
#include <cmath>

#include <memory>

namespace AetherSDR {

namespace {
// The receiver in FTX: the upper sideband, the decoders' audio band, and the
// narrowest receiver (48 ksps -- a view its 80 % holds).
constexpr int kFtxFilterLoHz = 100;
constexpr int kFtxFilterHiHz = 4000;
constexpr double kFtxPanBandwidthMhz = 0.0384;
}  // namespace

void MainWindow::activateFtx(int sliceId)
{
    SliceModel* s = m_radioModel.slice(sliceId);
    if (!s || !m_panStack)
        return;
    if (m_ftxSliceId == sliceId) {
        if (m_ftPanel) {
            m_ftPanel->raise();
            m_ftPanel->activateWindow();
        }
        return;
    }
    if (m_ftxSliceId >= 0)
        deactivateFtx();
#ifdef HAVE_RADE
    if (m_radeSliceId == sliceId)
        deactivateRADE();   // FreeDV on this receiver ends first
#endif
    PanadapterApplet* applet = m_panStack->panadapter(s->panId());
    if (!applet)
        return;

    // The mode's engine and window, once (they keep their state between the
    // sessions of the mode, as MicronSDR's do).
    if (!m_ftUnit) {
        m_ftUnit = new FtUnit(this);
        m_ftPanel = new FtPanel(m_ftUnit, this);
        // Build 382: the program's theme plus the rules it has none of (line
        // edits, spin boxes, checked buttons, tables, the progress bar), in
        // the colours of the panadapter's side menus (SpectrumOverlayMenu):
        // dark blue-grey fields with a lighter border, green for "on".
        ThemeManager::instance().applyStyleSheet(m_ftPanel, appStylesheetTemplate() + QStringLiteral(R"(
            QLabel, QCheckBox { background: transparent; }
            QPushButton {
                background-color: #1a2a3a; color: #c8d8e8;
                border: 1px solid #205070; border-radius: 3px; padding: 3px 9px;
            }
            QPushButton:hover { background-color: #204060; }
            QPushButton:checked { background-color: #006040; color: #00ff88; border-color: #00a060; }
            QPushButton:disabled { color: #5c6c7c; border-color: #263646; }
            QLineEdit, QSpinBox {
                background-color: #1a2a3a; color: #e8f0f8;
                border: 1px solid #304050; border-radius: 3px;
                padding: 1px 4px; min-height: 20px;
                selection-background-color: #205070;
            }
            QLineEdit { font-family: Consolas, "Cascadia Mono", monospace; }
            QLineEdit:focus, QSpinBox:focus { border-color: #0090e0; }
            QLineEdit:disabled, QSpinBox:disabled { color: #5c6c7c; }
            QComboBox {
                background-color: #1a2a3a; color: #e8f0f8;
                border: 1px solid #304050; border-radius: 3px; padding: 2px 6px; min-height: 20px;
            }
            QComboBox QAbstractItemView {
                background-color: #1a2a3a; color: #e8f0f8; selection-background-color: #205070;
            }
            QProgressBar { border: 1px solid #304050; border-radius: 2px; background-color: #0f1620; }
            QProgressBar::chunk { background-color: #00a060; }
            QTableWidget {
                background-color: #0f1620; alternate-background-color: #162230; color: #e6eef6;
                gridline-color: #22303f; border: 1px solid #304050;
                selection-background-color: #205070;
                font-family: Consolas, "Cascadia Mono", monospace; font-size: 12px;
            }
            QHeaderView::section {
                background-color: #1a2a3a; color: #c8d8e8; border: none;
                border-right: 1px solid #304050; padding: 2px 4px;
            }
            QTableCornerButton::section { background-color: #1a2a3a; border: none; }
        )"));
        m_ftPanel->loadSettings();
        // The dial of the mode -> the receiver (USB dial frequency).
        connect(m_ftPanel, &FtPanel::tuneRequest, this, [this](qint64 dialHz) {
            if (SliceModel* sl = m_radioModel.slice(m_ftxSliceId); sl && dialHz > 0)
                sl->setFrequency(static_cast<double>(dialHz) / 1.0e6);
        });
        connect(m_ftPanel, &FtPanel::modeChanged, this,
                [](int mode) { ftx::FtxState::instance().setSubMode(mode); });
        // Closing the window ends the mode. Queued: not inside its closeEvent.
        connect(m_ftPanel, &FtPanel::closed, this, [this] { deactivateFtx(); }, Qt::QueuedConnection);
        // The transmitter of the mode is the next stage of the port: refused.
        connect(m_ftPanel, &FtPanel::pttRequest, this, [this](bool on) {
            if (!on || !m_ftPanel)
                return;
            m_ftPanel->txAborted();
            m_ftPanel->txNote(QStringLiteral("Transmit is not connected yet in this build"));
        });
        m_ftPanel->setTxAllowed(false);
    }
    if (!m_ftUnit->load()) {
        QMessageBox::warning(this, QStringLiteral("FT8 / FT4 / WSPR"),
                             m_ftUnit->error()
                                 + QStringLiteral("\n\nlibftx.dll (from MicronSDR, third_party/libftx) "
                                                  "must be next to AetherSDR.exe."));
        return;
    }

    // What the receiver was, for the way out: frequency, mode, filter, the
    // panadapter's width.
    m_ftxSavedMode = s->mode();
    m_ftxSavedFreqMhz = s->frequency();
    m_ftxSavedFilterLo = s->filterLow();
    m_ftxSavedFilterHi = s->filterHigh();
    m_ftxSavedPanId = s->panId();
    m_ftxSavedPanBandwidthMhz = 0.0;
    if (const PanadapterModel* pan = m_radioModel.panadapter(m_ftxSavedPanId))
        m_ftxSavedPanBandwidthMhz = pan->bandwidthMhz();

    m_ftxSliceId = sliceId;
    ftx::FtxState::instance().sliceId = sliceId;   // the backend: no sound, no microphone

    if (s->mode() != QLatin1String("DIGU"))
        s->setMode(QStringLiteral("DIGU"));
    s->setFilterWidth(kFtxFilterLoHz, kFtxFilterHiHz);
    m_radioModel.requestPanBandwidth(m_ftxSavedPanId, kFtxPanBandwidthMhz);

    // The view: the panadapter (trace, waterfall, flag, filter, side buttons)
    // gives its place to the bar and the mode's waterfall.
    auto* view = new FtxView(applet);
    m_ftxView = view;
    applet->setFtxView(view);
    if (FtWaterfall* wf = m_ftPanel->waterfall())
        view->setWaterfall(wf, m_ftPanel->waterfallHeight());
    FtPanel* panel = m_ftPanel;
    auto dial = [view, panel, s] {
        view->setDialText(QStringLiteral("%1 MHz  USB").arg(s->frequency(), 0, 'f', 6));
        panel->setDialShown(static_cast<qint64>(std::llround(s->frequency() * 1.0e6)));
    };
    m_ftxFreqConn = connect(s, &SliceModel::frequencyChanged, view, [dial](double) { dial(); });

    // The receiver's own audio (before the loudspeaker's mute and fader) to
    // the decoders. FtUnit wants the receiver's I/Q at 48 kHz and demodulates
    // it itself (USB, 100 .. 4000 Hz); until this program hands it the I/Q,
    // it gets the demodulated audio as a real signal (I = audio, Q = 0): its
    // demodulator keeps the positive half, which IS that audio. So the
    // decoders hear the receiver's filter and AGC for now -- the next stage
    // gives them the I/Q, as in MicronSDR.
    auto up = std::make_shared<fdv::HalfbandResampler>();
    auto mono = std::make_shared<std::vector<float>>();
    auto a48 = std::make_shared<std::vector<float>>();
    auto zero = std::make_shared<std::vector<float>>();
    FtUnit* unit = m_ftUnit;
    m_ftxAudioConn = connect(&m_radioModel, &RadioModel::backendSliceAudioFrameReady, view,
                             [unit, sliceId, up, mono, a48, zero](int id, const PcmFrame& frame) {
        if (id != sliceId)
            return;
        const QVector<float>& st = frame.samples();
        const std::size_t frames = static_cast<std::size_t>(st.size() / 2);
        if (frames == 0)
            return;
        mono->resize(frames);
        for (std::size_t i = 0; i < frames; ++i)
            (*mono)[i] = 0.5f * (st[static_cast<qsizetype>(2 * i)] + st[static_cast<qsizetype>(2 * i + 1)]);
        up->up(mono->data(), frames, *a48);   // 48 kHz
        zero->assign(a48->size(), 0.0f);
        unit->iq(zero->data(), a48->data(), static_cast<unsigned long>(a48->size()));   // left = Q, right = I
        unit->audio(a48->data(), static_cast<unsigned long>(a48->size()));
    });
    connect(view, &FtxView::exitRequested, this, [this] { deactivateFtx(); }, Qt::QueuedConnection);
    // Any other mode set from elsewhere (CAT, TCI, a memory) ends FTX.
    m_ftxModeConn = connect(s, &SliceModel::modeChanged, this, [this](const QString& mode) {
        if (mode != QLatin1String("DIGU"))
            deactivateFtx(/*restoreMode=*/false);
    });
    m_ftxGoneConn = connect(s, &QObject::destroyed, this, [this] { deactivateFtx(false); });

    m_ftPanel->showWindow(geometry());
    m_ftPanel->start();   // -> tuneRequest: the receiver to the band's frequency
    dial();
    qInfo() << "MainWindow: FTX on, slice" << sliceId << "- was" << m_ftxSavedMode << m_ftxSavedFreqMhz
            << "MHz, filter" << m_ftxSavedFilterLo << ".." << m_ftxSavedFilterHi << "pan"
            << m_ftxSavedPanBandwidthMhz * 1000.0 << "kHz";
}

void MainWindow::deactivateFtx(bool restoreMode)
{
    if (m_ftxSliceId < 0)
        return;
    const int sliceId = m_ftxSliceId;
    m_ftxSliceId = -1;
    ftx::FtxState::instance().sliceId = -1;
    disconnect(m_ftxAudioConn);
    disconnect(m_ftxModeConn);
    disconnect(m_ftxFreqConn);
    disconnect(m_ftxGoneConn);

    if (m_ftPanel) {
        m_ftPanel->stop();
        m_ftPanel->hideWindow();   // its place and settings saved
    }
    if (auto* view = qobject_cast<FtxView*>(m_ftxView.data()))
        view->takeWaterfall();     // back to the FTX window before the view goes
    if (m_panStack) {
        if (PanadapterApplet* applet = m_panStack->panadapter(m_ftxSavedPanId))
            applet->setFtxView(nullptr);
    }
    if (m_ftxView) {
        m_ftxView->hide();
        m_ftxView->deleteLater();
        m_ftxView = nullptr;
    }
    if (m_ftxSavedPanBandwidthMhz > 0.0)
        m_radioModel.requestPanBandwidth(m_ftxSavedPanId, m_ftxSavedPanBandwidthMhz);
    // The frequency, the mode and its filter as they were -- unless another
    // mode ended FTX (it has brought its own passband; the frequency stays).
    if (restoreMode) {
        if (SliceModel* s = m_radioModel.slice(sliceId)) {
            if (!m_ftxSavedMode.isEmpty() && s->mode() != m_ftxSavedMode)
                s->setMode(m_ftxSavedMode);
            if (m_ftxSavedFilterHi > m_ftxSavedFilterLo)
                s->setFilterWidth(m_ftxSavedFilterLo, m_ftxSavedFilterHi);
            if (m_ftxSavedFreqMhz > 0.0)
                s->setFrequency(m_ftxSavedFreqMhz);
        }
    }
    qInfo() << "MainWindow: FTX off, slice" << sliceId;
}

}  // namespace AetherSDR
