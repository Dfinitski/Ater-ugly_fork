// ANAN fork build 380: the FTX mode (FT8 / FT4 / JS8 / WSPR) -- preparation.
// Entering and leaving the mode: what MicronSDR's ftEnter() / ftExit() do
// (micronsdr.cpp), for this program. See core/ftx/FtxState.h, gui/FtxView.h.
#include "MainWindow.h"

#include "FtxView.h"
#include "PanadapterApplet.h"
#include "PanadapterStack.h"
#include "core/PcmFrame.h"
#include "core/ftx/FtxState.h"
#include "models/PanadapterModel.h"
#include "models/SliceModel.h"

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
    if (m_ftxSliceId == sliceId)
        return;
    if (m_ftxSliceId >= 0)
        deactivateFtx();
#ifdef HAVE_RADE
    if (m_radeSliceId == sliceId)
        deactivateRADE();   // FreeDV on this receiver ends first
#endif
    PanadapterApplet* applet = m_panStack->panadapter(s->panId());
    if (!applet)
        return;

    // What the receiver was, for the way out ("the previous mode keeps its
    // parameters"): mode, filter, the panadapter's width.
    m_ftxSavedMode = s->mode();
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
    // gives its place to the FTX view.
    auto* view = new FtxView(applet);
    m_ftxView = view;
    applet->setFtxView(view);
    auto dial = [view, s] {
        view->setDialText(QStringLiteral("%1 MHz  USB").arg(s->frequency(), 0, 'f', 6));
    };
    dial();
    m_ftxFreqConn = connect(s, &SliceModel::frequencyChanged, view, [dial](double) { dial(); });
    // The receiver's own audio (before the loudspeaker's mute and fader).
    m_ftxAudioConn = connect(&m_radioModel, &RadioModel::backendSliceAudioFrameReady, view,
                             [view, sliceId](int id, const PcmFrame& frame) {
        if (id == sliceId)
            view->feedAudio(frame.samples());
    });
    // Queued: the view is deleted on the way out, not inside its own signal.
    connect(view, &FtxView::exitRequested, this, [this] { deactivateFtx(); }, Qt::QueuedConnection);
    // Any other mode set from elsewhere (CAT, TCI, a memory) ends FTX.
    m_ftxModeConn = connect(s, &SliceModel::modeChanged, this, [this](const QString& mode) {
        if (mode != QLatin1String("DIGU"))
            deactivateFtx(/*restoreMode=*/false);
    });
    m_ftxGoneConn = connect(s, &QObject::destroyed, this, [this] { deactivateFtx(false); });
    qInfo() << "MainWindow: FTX on, slice" << sliceId << "- was" << m_ftxSavedMode << "filter"
            << m_ftxSavedFilterLo << ".." << m_ftxSavedFilterHi << "pan"
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
    // The mode and its filter as they were -- unless another mode ended FTX
    // (it has brought its own passband).
    if (restoreMode) {
        if (SliceModel* s = m_radioModel.slice(sliceId)) {
            if (!m_ftxSavedMode.isEmpty() && s->mode() != m_ftxSavedMode)
                s->setMode(m_ftxSavedMode);
            if (m_ftxSavedFilterHi > m_ftxSavedFilterLo)
                s->setFilterWidth(m_ftxSavedFilterLo, m_ftxSavedFilterHi);
        }
    }
    qInfo() << "MainWindow: FTX off, slice" << sliceId;
}

}  // namespace AetherSDR
