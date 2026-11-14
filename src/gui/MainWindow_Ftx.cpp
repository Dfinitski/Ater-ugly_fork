// ANAN fork build 380/381: the FTX mode (FT8 / FT4 / WSPR; JS8 to come).
// Entering and leaving the mode: what MicronSDR's ftEnter() / ftExit() do
// (micronsdr.cpp), for this program. The window of the mode and its decoders
// are MicronSDR's own files (src/ftx: FtPanel, FtUnit, FtWaterfall, FtTx,
// FtNet, PskReporter; libftx.dll). See core/ftx/FtxState.h, gui/FtxView.h.
#include "MainWindow.h"

#include "FtxView.h"
#include "PanadapterApplet.h"
#include "PanadapterStack.h"
#include "SpectrumOverlayMenu.h"
#include "SpectrumWidget.h"
#include "Theme.h"
#include "core/PcmFrame.h"
#include "core/fdv/FdvEngine.h"   // HalfbandResampler: 24 -> 48 kHz
#include "core/ftx/FtxState.h"
#include "ftx/FtPanel.h"
#include "ftx/FtUnit.h"
#include "ftx/FtWaterfall.h"
#include "models/PanadapterModel.h"
#include "models/SliceModel.h"

#include <QDir>
#include <QImage>
#include <QDateTime>
#include <QMessageBox>
#include "core/backends/anan/AnanXvtr.h"
#include <QTimer>
#include <QPainter>
#include <cmath>

#include <memory>

namespace AetherSDR {

namespace {

// Build 384: the arrows of the spin boxes. A styled QSpinBox loses the native
// ones, and a stylesheet can only show an image there: two small triangles in
// the text colour, drawn once into the temporary directory (as
// RadioSetupDialog does for its own spin boxes).
QString ftxSpinArrowStyle()
{
    auto arrow = [](const QString& name, bool up) -> QString {
        const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("aethersdr-ftx-%1.png").arg(name));
        QImage img(16, 10, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(200, 216, 232));
            const QPointF upTri[] = {{2, 9}, {14, 9}, {8, 1}};
            const QPointF downTri[] = {{2, 1}, {14, 1}, {8, 9}};
            p.drawPolygon(up ? upTri : downTri, 3);
        }
        return img.save(path, "PNG") ? QString(path).replace(QLatin1Char('\\'), QLatin1Char('/')) : QString();
    };
    const QString up = arrow(QStringLiteral("up"), true);
    const QString down = arrow(QStringLiteral("down"), false);
    if (up.isEmpty() || down.isEmpty())
        return {};
    return QStringLiteral(
        "QSpinBox { padding-right: 18px; }"
        "QSpinBox::up-button, QSpinBox::down-button {"
        " subcontrol-origin: border; width: 16px; border: none;"
        " border-left: 1px solid {{color.border.strong}}; background: transparent; }"
        "QSpinBox::up-button { subcontrol-position: top right; }"
        "QSpinBox::down-button { subcontrol-position: bottom right; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: {{color.background.2}}; }"
        "QSpinBox::up-arrow { image: url(\"%1\"); width: 8px; height: 5px; }"
        "QSpinBox::down-arrow { image: url(\"%2\"); width: 8px; height: 5px; }"
        // Build 390: the program's theme has "QComboBox::drop-down { border:
        // none }" and no arrow image -- a styled combo box then shows nothing
        // that says "a list" (operator, 14.11). The same triangle.
        "QComboBox { padding-right: 20px; }"
        "QComboBox::drop-down { subcontrol-origin: border; subcontrol-position: center right;"
        " width: 18px; border: none; border-left: 1px solid {{color.border.strong}}; }"
        "QComboBox::drop-down:hover { background: {{color.background.2}}; }"
        "QComboBox::down-arrow { image: url(\"%2\"); width: 8px; height: 5px; }")
        .arg(up, down);
}

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
        // Build 382/384: the program's theme plus the rules it has none of
        // (line edits, spin boxes with their arrows, checked buttons, tables,
        // the progress bar, scroll bars). Buttons as the applet buttons of
        // the applet panel (AppletPanel kBarBtnStyle): the theme's colours,
        // "on" = the dim accent (blue) with a bright outline, the text stays
        // the theme's light text. Nothing green: that was MicronSDR's.
        ThemeManager::instance().applyStyleSheet(
            m_ftPanel, appStylesheetTemplate() + ftxSpinArrowStyle() + QStringLiteral(R"(
            QLabel, QCheckBox { background: transparent; }
            QPushButton {
                background: {{color.background.1}}; color: {{color.text.primary}};
                border: 1px solid {{color.background.2}}; border-radius: 3px; padding: 3px 9px;
            }
            QPushButton:hover { background: {{color.background.2}}; }
            QPushButton:checked {
                background: {{color.accent.dim}}; color: {{color.text.primary}};
                border: 1px solid {{color.accent.bright}};
            }
            QPushButton:disabled { color: {{color.text.disabled}}; border-color: {{color.background.1}}; }
            QLineEdit, QSpinBox {
                background-color: {{color.background.1}}; color: {{color.text.primary}};
                border: 1px solid {{color.border.strong}}; border-radius: 3px;
                padding: 1px 4px; min-height: 20px;
                selection-background-color: {{color.accent.dim}};
            }
            QLineEdit:focus, QSpinBox:focus { border-color: {{color.accent}}; }
            QLineEdit:disabled, QSpinBox:disabled { color: {{color.text.disabled}}; }
            QComboBox { min-height: 20px; }
            QComboBox QAbstractItemView {
                background-color: {{color.background.1}}; color: {{color.text.primary}};
                selection-background-color: {{color.accent.dim}};
            }
            QProgressBar {
                border: 1px solid {{color.border.strong}}; border-radius: 2px;
                background-color: {{color.background.0}};
            }
            QProgressBar::chunk { background-color: {{color.accent}}; }
            QTableWidget {
                background-color: {{color.background.0}};
                alternate-background-color: {{color.background.1}};
                color: {{color.text.primary}};
                gridline-color: {{color.background.2}}; border: 1px solid {{color.border.strong}};
                selection-background-color: {{color.accent.dim}};
            }
            QHeaderView::section {
                background-color: {{color.background.1}}; color: {{color.text.primary}}; border: none;
                border-right: 1px solid {{color.border.strong}}; padding: 2px 4px;
            }
            QTableCornerButton::section { background-color: {{color.background.1}}; border: none; }
            QScrollBar:vertical { background: {{color.background.0}}; width: 12px; margin: 0; border: none; }
            QScrollBar::handle:vertical {
                background: {{color.background.2}}; min-height: 24px; border-radius: 4px; margin: 2px;
            }
            QScrollBar::handle:vertical:hover { background: {{color.border.strong}}; }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }
            QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        )"));
        m_ftPanel->loadSettings();
        // The dial of the mode -> the receiver (USB dial frequency).
        connect(m_ftPanel, &FtPanel::tuneRequest, this, [this](qint64 dialHz) {
            if (dialHz > 0)
                ftxTune(static_cast<double>(dialHz) / 1.0e6);
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
    m_ftxSavedLocked = s->isLocked();
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
    // Build 385: "+RX" of the bar is the "+RX" of this panadapter's side menu
    // (hidden with the panadapter) -- the same signal, so the same handler.
    connect(view, &FtxView::addRxRequested, this, [applet = QPointer<PanadapterApplet>(applet)] {
        if (!applet || !applet->spectrumWidget())
            return;
        if (SpectrumOverlayMenu* menu = applet->spectrumWidget()->overlayMenu())
            emit menu->addRxClicked(applet->panId());
    });
    // Build 387 (operator, 14.11: "the mode change -- block it too"): a mode
    // set from elsewhere (CAT, TCI, MIDI, a memory) does not end FTX any
    // more; the receiver is put back to the mode's DIGU and filter. Queued:
    // not inside the model's own change. Only the window (closing it) and
    // Exit FTX leave the mode.
    m_ftxModeConn = connect(s, &SliceModel::modeChanged, this, [this, sliceId](const QString& mode) {
        if (mode == QLatin1String("DIGU") || m_ftxSliceId != sliceId)
            return;
        QMetaObject::invokeMethod(this, [this, sliceId] {
            SliceModel* sl = m_radioModel.slice(sliceId);
            if (!sl || m_ftxSliceId != sliceId)
                return;
            if (sl->mode() != QLatin1String("DIGU"))
                sl->setMode(QStringLiteral("DIGU"));
            sl->setFilterWidth(kFtxFilterLoHz, kFtxFilterHiHz);
        }, Qt::QueuedConnection);
    });
    // Build 387 (operator: "the receiver in FTX takes the transmitter; the
    // second receiver is blocked for transmit -- never"): the FTX receiver is
    // made the transmitting one and kept so; the backend refuses every key
    // that is not the mode's own. The one before the mode comes back on exit.
    m_ftxSavedTxSliceId = -1;
    for (SliceModel* other : m_radioModel.slices()) {
        if (other && other->isTxSlice())
            m_ftxSavedTxSliceId = other->sliceId();
    }
    if (!s->isTxSlice())
        s->setTxSlice(true);
    m_ftxTxSliceConn = connect(s, &SliceModel::txSliceChanged, this, [this, sliceId](bool tx) {
        if (tx || m_ftxSliceId != sliceId)
            return;
        QMetaObject::invokeMethod(this, [this, sliceId] {
            SliceModel* sl = m_radioModel.slice(sliceId);
            if (sl && m_ftxSliceId == sliceId && !sl->isTxSlice())
                sl->setTxSlice(true);
        }, Qt::QueuedConnection);
    });
    // The attenuator: AUTO ATT steps only between the periods (the backend
    // asks; MicronSDR: FtUnit::levelGap).
    ftx::FtxState::instance().levelGap = [unit = m_ftUnit] {
        return unit->levelGap(QDateTime::currentMSecsSinceEpoch());
    };
    m_ftxGoneConn = connect(s, &QObject::destroyed, this, [this] { deactivateFtx(false); });
    // Build 386: the frequency of this receiver is the FTX window's alone.
    // Locked now; should anything unlock it (a MIDI or TCI "lock off"), it is
    // locked again -- only ftxTune() opens it, for its own call.
    if (!s->isLocked())
        s->setLocked(true);
    m_ftxLockConn = connect(s, &SliceModel::lockedChanged, this, [this, sliceId](bool locked) {
        if (locked || m_ftxTuning || m_ftxSliceId != sliceId)
            return;
        if (SliceModel* sl = m_radioModel.slice(sliceId))
            sl->setLocked(true);
    });
    // A receiver opened while FTX is on (the bar's +RX) is built as a copy of
    // the first one -- which is now the FTX receiver: DIGU, 100 .. 4000 Hz, on
    // the FT frequency. It gets what that receiver was BEFORE the mode
    // instead: an ordinary receiver, as if FTX were not there.
    m_ftxNewSliceConn = connect(&m_radioModel, &RadioModel::sliceAdded, this, [this](SliceModel* added) {
        if (!added || m_ftxSliceId < 0 || added->sliceId() == m_ftxSliceId)
            return;
        if (!m_ftxSavedMode.isEmpty() && added->mode() != m_ftxSavedMode)
            added->setMode(m_ftxSavedMode);
        if (m_ftxSavedFilterHi > m_ftxSavedFilterLo)
            added->setFilterWidth(m_ftxSavedFilterLo, m_ftxSavedFilterHi);
        if (m_ftxSavedFreqMhz > 0.0 && !added->isLocked())
            added->setFrequency(m_ftxSavedFreqMhz);
        ftxUpdateAddRx();
        QTimer::singleShot(0, this, [this, id = added->sliceId()] {
            if (m_ftxSliceId >= 0 && m_radioModel.slice(id))
                setActiveSlice(id);   // the applets to the ordinary receiver
        });
        qInfo() << "MainWindow: FTX is on -- the new receiver" << added->sliceId() << "starts as"
                << m_ftxSavedMode << m_ftxSavedFreqMhz << "MHz, not as a copy of the FTX receiver";
    });

    // Build 388: "+RX" only while there is no second receiver; back when it
    // is closed.
    m_ftxSliceGoneConn = connect(&m_radioModel, &RadioModel::sliceRemoved, this,
                                 [this](int) { QTimer::singleShot(0, this, [this] { ftxUpdateAddRx(); }); });
    ftxUpdateAddRx();
    // The applets follow the ACTIVE receiver (its A / B buttons): with a
    // second receiver open they belong to that one, not to the FTX receiver
    // (MainWindow::setActiveSliceInternal refuses the FTX one then).
    if (m_activeSliceId == sliceId) {
        for (SliceModel* other : m_radioModel.slices()) {
            if (other && other->sliceId() != sliceId) {
                setActiveSlice(other->sliceId());
                break;
            }
        }
    }
    // Build 390: the bands above the radio's own coverage are offered when a
    // transverter of Radio Setup carries them (asked each time the list is
    // filled, so one set up later appears with the next change of mode/band).
    m_ftPanel->setBandReach([this](qint64 hz) {
        const RadioCapabilities caps = m_radioModel.backendCapabilities();
        return anan::AnanXvtrStore::instance().forRf(static_cast<double>(hz), caps.tuningMinHz,
                                                     caps.tuningMaxHz) != nullptr;
    });
    m_ftPanel->showWindow(geometry());
    m_ftPanel->start();   // -> tuneRequest: the receiver to the band's frequency
    dial();
    qInfo() << "MainWindow: FTX on, slice" << sliceId << "- was" << m_ftxSavedMode << m_ftxSavedFreqMhz
            << "MHz, filter" << m_ftxSavedFilterLo << ".." << m_ftxSavedFilterHi << "pan"
            << m_ftxSavedPanBandwidthMhz * 1000.0 << "kHz";
}

void MainWindow::ftxUpdateAddRx()
{
    if (auto* view = qobject_cast<FtxView*>(m_ftxView.data()))
        view->setAddRxVisible(m_radioModel.slices().size() < 2);
}

void MainWindow::ftxTune(double mhz)
{
    SliceModel* s = m_radioModel.slice(m_ftxSliceId);
    if (!s || mhz <= 0.0)
        return;
    m_ftxTuning = true;
    if (s->isLocked())
        s->setLocked(false);
    s->setFrequency(mhz);
    s->setLocked(true);
    m_ftxTuning = false;
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
    disconnect(m_ftxLockConn);
    disconnect(m_ftxNewSliceConn);
    disconnect(m_ftxTxSliceConn);
    disconnect(m_ftxSliceGoneConn);
    ftx::FtxState::instance().levelGap = nullptr;
    // The mode's lock off, so the frequency below can go back; the lock the
    // operator had before the mode is put back at the end.
    if (SliceModel* s = m_radioModel.slice(sliceId); s && s->isLocked())
        s->setLocked(false);

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
            if (m_ftxSavedFreqMhz > 0.0 && !s->isLocked())
                s->setFrequency(m_ftxSavedFreqMhz);
        }
    }
    if (m_ftxSavedLocked) {
        if (SliceModel* s = m_radioModel.slice(sliceId))
            s->setLocked(true);
    }
    // The transmitting receiver as before the mode.
    if (m_ftxSavedTxSliceId >= 0 && m_ftxSavedTxSliceId != sliceId) {
        if (SliceModel* was = m_radioModel.slice(m_ftxSavedTxSliceId); was && !was->isTxSlice())
            was->setTxSlice(true);
    }
    qInfo() << "MainWindow: FTX off, slice" << sliceId;
}

}  // namespace AetherSDR
