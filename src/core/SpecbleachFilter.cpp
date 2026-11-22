#ifdef HAVE_SPECBLEACH

#include "SpecbleachFilter.h"
#include "core/dsp/FftwPlannerLock.h"
#include <specbleach_denoiser.h>
#include <cstring>
#include <algorithm>
#include <QDebug>

namespace AetherSDR {

static constexpr float kFrameSizeMs = 40.0f;  // 40ms frames (~960 samples)

SpecbleachFilter::SpecbleachFilter(int sampleRate)
    : m_sampleRate(sampleRate)
{
    if (sampleRate != 24000 && sampleRate != 48000) {
        qWarning() << "SpecbleachFilter: unsupported sample rate" << sampleRate;
        return;
    }
    {
        auto lock = fftwfPlannerLock();
        for (auto& handle : m_handles) {
            handle = specbleach_initialize(sampleRate, kFrameSizeMs);
        }
    }
    if (!isValid()) {
        qWarning() << "SpecbleachFilter: failed to initialize";
        return;
    }
    applyParams();
    qDebug() << "SpecbleachFilter: initialized, latency ="
             << specbleach_get_latency(m_handles[0]) << "samples";
}

SpecbleachFilter::~SpecbleachFilter()
{
    auto lock = fftwfPlannerLock();
    for (auto handle : m_handles) {
        if (handle) {
            specbleach_free(handle);
        }
    }
}

void SpecbleachFilter::reset()
{
    m_frameCount = 0;
    for (auto handle : m_handles) {
        if (handle) {
            specbleach_reset_noise_profile(handle);
        }
    }
}

void SpecbleachFilter::applyParams()
{
    if (!isValid()) return;

    // ANAN fork build 589: without adaptive estimation libspecbleach reduces
    // against a LEARNED profile, and nothing ever set learn_noise -- the
    // profile stayed empty and NR4 with "Adaptive" off did nothing. It now
    // learns during the first second, the pass-through period (#827) that
    // was meant for it; the library finalizes the profile when learning
    // stops.
    const bool learn = !m_adaptive.load() && m_frameCount < kLearningFrames;
    m_learning = learn;
    SpectralBleachDenoiserParameters params{};
    params.learn_noise = learn ? 1 : 0;
    params.residual_listen = false;
    params.reduction_amount = m_reduction.load();
    params.smoothing_factor = m_smoothing.load();
    params.whitening_factor = m_whitening.load();
    params.adaptive_noise = m_adaptive.load() ? 1 : 0;
    params.noise_estimation_method = m_noiseMethod.load();
    params.masking_depth = m_maskingDepth.load();
    // Build 589: specbleach_load_parameters() divides this one by 100 (its
    // header says 0..1, its code takes percent): 0..1 here reached the
    // Berouti factor as 0..0.01, and the Suppression slider did nothing.
    params.suppression_strength = m_suppression.load() * 100.0f;
    params.aggressiveness = 0.0f;
    params.tonal_reduction = 0.0f;

    for (auto handle : m_handles) {
        specbleach_load_parameters(handle, params);
    }
    m_paramsDirty = false;
}

QByteArray SpecbleachFilter::process(const QByteArray& pcmStereo)
{
    if (!isValid())
        return pcmStereo;

    // Build 589: "Adaptive" switched off -- learn a profile anew.
    if (m_relearn.exchange(false))
        reset();
    // Apply parameter changes if dirty, and when the learning period
    // starts or ends (build 589).
    if (m_paramsDirty.load()
        || m_learning != (!m_adaptive.load() && m_frameCount < kLearningFrames))
        applyParams();

    const int totalFloats = pcmStereo.size() / static_cast<int>(sizeof(float));
    const int frames = totalFloats / 2;
    if (frames <= 0)
        return pcmStereo;

    // Resize buffers if needed
    if (static_cast<int>(m_channelIn[0].size()) < frames) {
        for (int channel = 0; channel < 2; ++channel) {
            m_channelIn[channel].resize(frames);
            m_channelOut[channel].resize(frames);
        }
    }

    const auto* in = reinterpret_cast<const float*>(pcmStereo.constData());
    for (int i = 0; i < frames; ++i) {
        m_channelIn[0][i] = in[i * 2];
        m_channelIn[1][i] = in[i * 2 + 1];
    }

    // Process — feed audio to build noise profile even during learning
    for (int channel = 0; channel < 2; ++channel) {
        specbleach_process(m_handles[channel], frames,
                           m_channelIn[channel].data(),
                           m_channelOut[channel].data());
    }

    // During the learning period, pass original audio through so the user
    // hears unprocessed audio instead of silence while the noise profile
    // builds. The library still receives the audio above for profiling. (#827)
    if (m_frameCount < kLearningFrames) {
        ++m_frameCount;
        return pcmStereo;
    }

    QByteArray output(pcmStereo.size(), Qt::Uninitialized);
    auto* out = reinterpret_cast<float*>(output.data());
    for (int i = 0; i < frames; ++i) {
        out[i * 2] = std::clamp(m_channelOut[0][i], -1.0f, 1.0f);
        out[i * 2 + 1] = std::clamp(m_channelOut[1][i], -1.0f, 1.0f);
    }
    return output;
}

// Parameter setters — mark dirty so next process() applies them
void SpecbleachFilter::setReductionAmount(float dB)   { m_reduction = std::clamp(dB, 0.0f, 40.0f); m_paramsDirty = true; }
void SpecbleachFilter::setSmoothingFactor(float pct)   { m_smoothing = std::clamp(pct, 0.0f, 100.0f); m_paramsDirty = true; }
void SpecbleachFilter::setWhiteningFactor(float pct)   { m_whitening = std::clamp(pct, 0.0f, 100.0f); m_paramsDirty = true; }
void SpecbleachFilter::setAdaptiveNoise(bool on)
{
    if (m_adaptive.exchange(on) && !on)
        m_relearn = true;   // build 589: the manual mode needs a profile
    m_paramsDirty = true;
}
void SpecbleachFilter::setNoiseEstimationMethod(int m) { m_noiseMethod = std::clamp(m, 0, 2); m_paramsDirty = true; }
void SpecbleachFilter::setMaskingDepth(float v)        { m_maskingDepth = std::clamp(v, 0.0f, 1.0f); m_paramsDirty = true; }
void SpecbleachFilter::setSuppressionStrength(float v) { m_suppression = std::clamp(v, 0.0f, 1.0f); m_paramsDirty = true; }

} // namespace AetherSDR

#endif // HAVE_SPECBLEACH
