#pragma once

/* AetherSDR: Dream's DRM receiver behind a plain C++ interface.
 *
 * The only header of third_party/dream that AetherSDR includes. It names no
 * Dream type on purpose: Dream's own headers change meaning with QT_CORE_LIB
 * (QMutex vs. its own mutex, ...) and AetherSDR's Qt code defines it, so they
 * must only ever be compiled inside the aether_dream library.
 *
 * One engine = one receiver. It runs Dream on its own thread:
 *   pushIq()    zero-IF I/Q centred on the DRM signal (any thread)
 *   pullAudio() decoded stereo audio (any thread)
 * both at the rate given to start(): 24000 (Dream upsamples the signal x2
 * itself) or 48000.
 *   status()    a snapshot, refreshed about five times a second
 * GPL v2 or later, like the rest of third_party/dream. */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aether_drm {

enum class Light { Off, Ok, Error, Absent };

struct ServiceInfo {
    bool present = false;
    bool audio = false;
    uint32_t id = 0;
    std::string label;
    std::string codec;          // "AAC", "HE-AAC", "xHE-AAC", "Opus", "MOT Slideshow", ...
    double bitrateKbps = 0.0;
    std::string language;
    std::string country;
    bool decodable = true;      // false: no decoder here for this audio codec
};

struct Status {
    bool running = false;
    bool fdkAvailable = false;  // FDK-AAC found (AAC / xHE-AAC audio)
    Light io = Light::Off, timeSync = Light::Off, freqSync = Light::Off;
    Light fac = Light::Off, sdc = Light::Off, msc = Light::Off;
    bool synced = false;        // FAC decoded: the channel fields below are valid
    double snrDb = 0.0;
    double freqOffsetHz = 0.0;  // DC offset of the signal Dream tracks
    char robustness = 0;        // 'A'..'E'
    double bandwidthKHz = 0.0;
    std::string interleaver, mscQam, sdcQam;
    ServiceInfo services[4];
    int audioService = -1;      // the services[] index being played
    int dataService = -1;       // the services[] index whose data is decoded
    std::string textMessage;
    std::string utcTime;        // "2026-11-10 21:34 UTC" when the station sends it
    uint64_t slideSerial = 0;   // bumps when a new slideshow picture arrived
    // Journaline: the page asked for with requestJournalinePage() (0 = the
    // main menu) as text lines -- the title, then one line per item; an item
    // that links on is prefixed "[id] " with the object id to request.
    int journalinePage = 0;
    std::vector<std::string> journaline;
    double inputLevelDbfs = -150.0;
};

struct Slide {
    std::vector<uint8_t> data;  // the picture file as sent (JPEG / PNG)
    std::string name;
    std::string format;
    uint64_t serial = 0;
};

struct Settings {
    int freqInterpolation = 1;  // 0 linear, 1 Wiener, 2 DFT
    int timeInterpolation = 1;  // 0 linear, 1 Wiener
    int mlcIterations = 1;      // 0..4
    bool flipSpectrum = false;
};

class DrmEngine {
public:
    DrmEngine();
    ~DrmEngine();
    DrmEngine(const DrmEngine&) = delete;
    DrmEngine& operator=(const DrmEngine&) = delete;

    void start(int sampleRateHz = 48000);
    void stop();
    [[nodiscard]] bool running() const;
    [[nodiscard]] int sampleRate() const;

    // Interleaved I,Q floats at the start() rate (any scale: the engine keeps
    // its own slow input gain).
    void pushIq(const float* iq, std::size_t frames);
    // Up to maxFrames interleaved L,R floats at the start() rate; returns
    // frames written.
    std::size_t pullAudio(float* stereo, std::size_t maxFrames);
    [[nodiscard]] std::size_t audioFramesAvailable() const;

    [[nodiscard]] Status status() const;
    // The newest picture if its serial is above `after`.
    bool latestSlide(uint64_t after, Slide& out) const;
    void requestJournalinePage(int objId);

    void selectService(int index);   // services[] index; audio or data
    void setSettings(const Settings& s);

    static bool fdkAacAvailable();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace aether_drm
