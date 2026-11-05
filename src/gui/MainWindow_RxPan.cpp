// Receive-pan helpers that used to live in MainWindow_KiwiSdr.cpp.
//
// ANAN-100D port: KiwiSDR was removed, but these three methods are not Kiwi
// specific — they choose which slice's pan feeds the audio engine and apply a
// pan's bandwidth limits to its spectrum widget. Their Kiwi branches (a slice
// replaced by a KiwiSDR receiver, a pan widened to show a Kiwi) are gone.

#include "MainWindow.h"

#include "SpectrumWidget.h"
#include "core/AudioEngine.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

namespace AetherSDR {

SliceModel* MainWindow::flexRxPanSourceSlice() const
{
    auto isRadioBacked = [this](const SliceModel* slice) {
        return slice && m_radioModel.sliceMayBelongToUs(slice->sliceId());
    };

    SliceModel* active = m_radioModel.slice(m_activeSliceId);
    if (isRadioBacked(active)) {
        return active;
    }

    if (active && active->diversity()) {
        for (SliceModel* slice : m_radioModel.slices()) {
            if (!isRadioBacked(slice) || !slice->diversity()
                || slice->sliceId() == active->sliceId()) {
                continue;
            }
            if ((active->isDiversityChild() && slice->isDiversityParent())
                || (active->isDiversityParent() && slice->isDiversityChild())) {
                return slice;
            }
        }

        for (SliceModel* slice : m_radioModel.slices()) {
            if (isRadioBacked(slice) && slice->diversity()
                && slice->sliceId() != active->sliceId()) {
                return slice;
            }
        }
    }

    SliceModel* onlyUnmutedSlice = nullptr;
    for (SliceModel* slice : m_radioModel.slices()) {
        if (!isRadioBacked(slice) || slice->flexAudioMute()) {
            continue;
        }
        if (onlyUnmutedSlice) {
            return nullptr;
        }
        onlyUnmutedSlice = slice;
    }
    return onlyUnmutedSlice;
}

void MainWindow::syncFlexRxPanToAudioEngine()
{
    if (!m_audio) {
        return;
    }
    if (SliceModel* slice = flexRxPanSourceSlice()) {
        m_audio->setRxPan(slice->flexAudioPan());
    }
}

void MainWindow::applyPanBandwidthLimitsToWidget(const QString& panId,
                                                 SpectrumWidget* spectrum,
                                                 double minMhz,
                                                 double maxMhz) const
{
    Q_UNUSED(panId);
    if (!spectrum) {
        return;
    }
    spectrum->setBandwidthLimits(minMhz, maxMhz);
}

} // namespace AetherSDR
