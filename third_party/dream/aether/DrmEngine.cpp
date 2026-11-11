/* AetherSDR: Dream's DRM receiver behind a plain C++ interface (DrmEngine.h).
 * GPL v2 or later, like the rest of third_party/dream. */

#include "DrmEngine.h"

#include "../src/DrmReceiver.h"
#include "../src/datadecoding/DABMOT.h"
#include "../src/datadecoding/DataDecoder.h"
#include "../src/datadecoding/Journaline.h"
#include "../src/sound/soundinterface.h"
#include "../src/tables/TableFAC.h"
#include "../src/util/Settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

extern "C" bool aether_fdk_aac_available();

namespace aether_drm {

namespace {

constexpr std::size_t kMaxInputSamples = 2 * 48000 * 2;   // 2 s of I/Q shorts at 48 kHz
constexpr std::size_t kMaxAudioSamples = 2 * 48000 * 2;   // 2 s of L/R floats at 48 kHz
constexpr double kTargetRms = 3000.0;    // about -21 dBFS in shorts: OFDM peaks fit
constexpr double kGainTauS = 2.0;        // input gain follows slowly (OFDM, not speech)

Light lightFor(ETypeRxStatus s)
{
    switch (s) {
    case RX_OK:       return Light::Ok;
    case CRC_ERROR:
    case DATA_ERROR:  return Light::Error;
    case NOT_PRESENT: return Light::Off;
    }
    return Light::Off;
}

std::string qamFor(ECodScheme cs)
{
    switch (cs) {
    case CS_1_SM:    return "4-QAM";
    case CS_2_SM:    return "16-QAM";
    case CS_3_SM:
    case CS_3_HMSYM:
    case CS_3_HMMIX: return "64-QAM";
    }
    return {};
}

double bandwidthFor(ESpecOcc so)
{
    switch (so) {
    case SO_0: return 4.5;
    case SO_1: return 5.0;
    case SO_2: return 9.0;
    case SO_3: return 10.0;
    case SO_4: return 18.0;
    case SO_5: return 20.0;
    }
    return 0.0;
}

std::string codecFor(const CService& s)
{
    if (s.eAudDataFlag == CService::SF_DATA) {
        switch (s.DataParam.iUserAppIdent) {
        case DAB_AT_MOTSLIDESHOW: return "MOT Slideshow";
        case DAB_AT_JOURNALINE:   return "Journaline";
        case DAB_AT_BROADCASTWEBSITE: return "Broadcast Website";
        case DAB_AT_EPG:          return "EPG";
        case DAB_AT_TPEG:         return "TPEG";
        default:                  return "Data";
        }
    }
    switch (s.AudioParam.eAudioCoding) {
    case CAudioParam::AC_AAC:
        return s.AudioParam.eSBRFlag == CAudioParam::SB_USED ? "HE-AAC" : "AAC";
    case CAudioParam::AC_xHE_AAC: return "xHE-AAC";
    case CAudioParam::AC_OPUS:    return "Opus";
    case CAudioParam::AC_MPEGAAC: return "MPEG AAC";
    default:                      return "?";
    }
}

// The engine's sound card: Dream reads I/Q from it and writes audio to it.
class HostInput final : public CSoundInInterface {
public:
    std::mutex m;
    std::condition_variable cv;
    std::deque<short> q;
    bool stopping = false;

    bool Init(int, int, bool) override { return true; }
    bool Read(CVector<short>& v, CParameter&) override
    {
        std::unique_lock<std::mutex> lk(m);
        const auto need = static_cast<std::size_t>(v.Size());
        cv.wait(lk, [&] { return stopping || q.size() >= need; });
        if (stopping) {
            for (int i = 0; i < v.Size(); ++i)
                v[i] = 0;
            return false;
        }
        for (int i = 0; i < v.Size(); ++i) {
            v[i] = q.front();
            q.pop_front();
        }
        return false;   // false = no error
    }
    void Close() override {}
    std::string GetVersion() override { return "AetherSDR I/Q"; }
    CSoundInInterface* GetItem() override { return this; }
    void Enumerate(std::vector<std::string>&, std::vector<std::string>&, std::string&) override {}
    std::string GetItemName() override { return {}; }
    void SetItem(std::string) override {}
};

class HostOutput final : public CSoundOutInterface {
public:
    mutable std::mutex m;
    std::deque<float> q;

    bool Init(int, int, bool) override { return true; }
    bool Write(CVector<short>& v) override
    {
        std::lock_guard<std::mutex> lk(m);
        for (int i = 0; i < v.Size(); ++i)
            q.push_back(static_cast<float>(v[i]) / 32768.0f);
        while (q.size() > kMaxAudioSamples)
            q.pop_front();
        return false;
    }
    void Close() override {}
    std::string GetVersion() override { return "AetherSDR audio"; }
    CSoundOutInterface* GetItem() override { return this; }
    void Enumerate(std::vector<std::string>&, std::vector<std::string>&, std::string&) override {}
    std::string GetItemName() override { return {}; }
    void SetItem(std::string) override {}
};

} // namespace

struct DrmEngine::Impl {
    HostInput in;
    HostOutput out;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<bool> isRunning{false};
    int rate = 48000;

    // Input gain (pushIq thread only)
    double gain = 1000.0;
    double powerAvg = 0.0;
    double levelDbfs = -150.0;

    // Snapshot and requests, under `m`
    mutable std::mutex m;
    Status st;
    Slide slide;
    Settings settings;
    bool settingsDirty = true;
    int selectRequest = -1;
    int journalineReq = 0;

    void run();
    void applyRequests(CDRMReceiver& rx);
    void refresh(CDRMReceiver& rx, int& autoDataFor, uint64_t& slideSerial);
};

DrmEngine::DrmEngine()
    : d(std::make_unique<Impl>())
{
}

DrmEngine::~DrmEngine()
{
    stop();
}

bool DrmEngine::fdkAacAvailable()
{
    return aether_fdk_aac_available();
}

void DrmEngine::start(int sampleRateHz)
{
    if (d->worker.joinable())
        return;
    d->rate = sampleRateHz <= 36000 ? 24000 : 48000;
    d->powerAvg = 0.0;
    {
        std::lock_guard<std::mutex> lk(d->in.m);
        d->in.stopping = false;
        d->in.q.clear();
    }
    {
        std::lock_guard<std::mutex> lk(d->out.m);
        d->out.q.clear();
    }
    {
        std::lock_guard<std::mutex> lk(d->m);
        d->st = Status();
        d->st.fdkAvailable = aether_fdk_aac_available();
        d->settingsDirty = true;
    }
    d->stop = false;
    d->worker = std::thread([this] { d->run(); });
}

void DrmEngine::stop()
{
    if (!d->worker.joinable())
        return;
    d->stop = true;
    {
        std::lock_guard<std::mutex> lk(d->in.m);
        d->in.stopping = true;
    }
    d->in.cv.notify_all();
    d->worker.join();
    std::lock_guard<std::mutex> lk(d->m);
    d->st.running = false;
}

bool DrmEngine::running() const
{
    return d->isRunning;
}

int DrmEngine::sampleRate() const
{
    return d->rate;
}

void DrmEngine::pushIq(const float* iq, std::size_t frames)
{
    if (!d->isRunning || frames == 0)
        return;
    // Slow input gain to keep the 16-bit input of Dream well used.
    double p = 0.0;
    for (std::size_t i = 0; i < 2 * frames; ++i)
        p += static_cast<double>(iq[i]) * iq[i];
    p /= static_cast<double>(2 * frames);
    const double dt = static_cast<double>(frames) / d->rate;
    const double a = 1.0 - std::exp(-dt / kGainTauS);
    d->powerAvg = d->powerAvg <= 0.0 ? p : d->powerAvg + a * (p - d->powerAvg);
    const double rms = std::sqrt(std::max(d->powerAvg, 1e-20));
    d->gain = std::clamp(kTargetRms / rms, 1.0, 1e9);
    d->levelDbfs = 20.0 * std::log10(std::max(rms, 1e-10));

    {
        std::lock_guard<std::mutex> lk(d->in.m);
        for (std::size_t i = 0; i < 2 * frames; ++i) {
            const double v = std::clamp(static_cast<double>(iq[i]) * d->gain, -32767.0, 32767.0);
            d->in.q.push_back(static_cast<short>(std::lround(v)));
        }
        while (d->in.q.size() > kMaxInputSamples)
            d->in.q.pop_front();
    }
    d->in.cv.notify_one();
}

std::size_t DrmEngine::pullAudio(float* stereo, std::size_t maxFrames)
{
    std::lock_guard<std::mutex> lk(d->out.m);
    const std::size_t frames = std::min(maxFrames, d->out.q.size() / 2);
    for (std::size_t i = 0; i < 2 * frames; ++i) {
        stereo[i] = d->out.q.front();
        d->out.q.pop_front();
    }
    return frames;
}

std::size_t DrmEngine::audioFramesAvailable() const
{
    std::lock_guard<std::mutex> lk(d->out.m);
    return d->out.q.size() / 2;
}

Status DrmEngine::status() const
{
    std::lock_guard<std::mutex> lk(d->m);
    Status s = d->st;
    s.inputLevelDbfs = d->levelDbfs;
    return s;
}

bool DrmEngine::latestSlide(uint64_t after, Slide& out) const
{
    std::lock_guard<std::mutex> lk(d->m);
    if (d->slide.serial <= after || d->slide.data.empty())
        return false;
    out = d->slide;
    return true;
}

void DrmEngine::requestJournalinePage(int objId)
{
    std::lock_guard<std::mutex> lk(d->m);
    d->journalineReq = std::max(0, objId);
}

void DrmEngine::selectService(int index)
{
    std::lock_guard<std::mutex> lk(d->m);
    d->selectRequest = index;
}

void DrmEngine::setSettings(const Settings& s)
{
    std::lock_guard<std::mutex> lk(d->m);
    d->settings = s;
    d->settingsDirty = true;
}

void DrmEngine::Impl::applyRequests(CDRMReceiver& rx)
{
    Settings s;
    bool dirty = false;
    int sel = -1;
    {
        std::lock_guard<std::mutex> lk(m);
        if (settingsDirty) {
            s = settings;
            dirty = true;
            settingsDirty = false;
        }
        sel = selectRequest;
        selectRequest = -1;
    }
    if (dirty) {
        static const ETypeIntFreq kFreq[3] = {FLINEAR, FWIENER, FDFTFILTER};
        rx.SetFreqInt(kFreq[std::clamp(s.freqInterpolation, 0, 2)]);
        rx.SetTimeInt(s.timeInterpolation == 0 ? TLINEAR : TWIENER);
        rx.GetMSCMLC()->SetNumIterations(std::clamp(s.mlcIterations, 0, 4));
        rx.GetReceiveData()->SetFlippedSpectrum(s.flipSpectrum);
    }
    if (sel >= 0 && sel < MAX_NUM_SERVICES) {
        CParameter& P = *rx.GetParameters();
        P.Lock();
        if (P.Service[sel].IsActive()) {
            if (P.Service[sel].eAudDataFlag == CService::SF_AUDIO)
                P.SetCurSelAudioService(sel);
            else
                P.SetCurSelDataService(sel);
        }
        P.Unlock();
    }
}

void DrmEngine::Impl::refresh(CDRMReceiver& rx, int& autoDataFor, uint64_t& slideSerial)
{
    CParameter& P = *rx.GetParameters();
    Status s;
    s.running = true;
    s.fdkAvailable = aether_fdk_aac_available();

    P.Lock();
    auto& R = P.ReceiveStatus;   // GetStatus() is not const
    s.io = lightFor(R.InterfaceI.GetStatus());
    s.timeSync = lightFor(R.TSync.GetStatus());
    s.freqSync = lightFor(R.FSync.GetStatus());
    s.fac = lightFor(R.FAC.GetStatus());
    s.sdc = lightFor(R.SDC.GetStatus());
    s.msc = lightFor(R.SLAudio.GetStatus());
    s.synced = rx.GetAcquiState() == AS_WITH_SIGNAL && s.fac == Light::Ok;
    if (s.synced) {
        s.snrDb = P.GetSNR();
        s.freqOffsetHz = P.GetDCFrequency();
        const ERobMode rm = P.GetWaveMode();
        if (rm != RM_NO_MODE_DETECTED)
            s.robustness = static_cast<char>('A' + static_cast<int>(rm));
        s.bandwidthKHz = bandwidthFor(P.GetSpectrumOccup());
        s.interleaver = P.eSymbolInterlMode == CParameter::SI_LONG ? "long" : "short";
        s.mscQam = qamFor(P.eMSCCodingScheme);
        s.sdcQam = qamFor(P.eSDCCodingScheme);
    }

    int firstData = -1;
    for (int k = 0; k < 4 && k < MAX_NUM_SERVICES; ++k) {
        const CService& sv = P.Service[k];
        ServiceInfo& o = s.services[k];
        o.present = s.synced && sv.IsActive();
        if (!o.present)
            continue;
        o.audio = sv.eAudDataFlag == CService::SF_AUDIO;
        o.id = sv.iServiceID;
        o.label = sv.strLabel;
        o.codec = codecFor(sv);
        o.bitrateKbps = P.GetBitRateKbps(k, !o.audio);
        if (sv.iLanguage >= 0 && sv.iLanguage < LEN_TABLE_LANGUAGE_CODE && sv.iLanguage != 0)
            o.language = strTableLanguageCode[sv.iLanguage];
        o.country = sv.strCountryCode;
        if (o.audio) {
            const auto c = sv.AudioParam.eAudioCoding;
            o.decodable = (c == CAudioParam::AC_AAC || c == CAudioParam::AC_xHE_AAC)
                ? s.fdkAvailable : true;
        } else if (firstData < 0) {
            firstData = k;
        }
    }
    s.audioService = P.GetCurSelAudioService();
    s.dataService = P.GetCurSelDataService();
    if (s.audioService >= 0 && s.audioService < 4 && s.services[s.audioService].present)
        s.textMessage = P.Service[s.audioService].AudioParam.strTextMessage;
    if (P.iYear > 0) {
        char buf[48];
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d UTC", P.iYear, P.iMonth, P.iDay,
                      P.iUTCHour, P.iUTCMin);
        s.utcTime = buf;
    }
    // Decode the first data service unless the operator chose one.
    const bool dataSelOk = s.dataService >= 0 && s.dataService < 4
        && s.services[s.dataService].present && !s.services[s.dataService].audio;
    if (!dataSelOk && firstData >= 0 && autoDataFor != firstData) {
        P.SetCurSelDataService(firstData);
        autoDataFor = firstData;
        s.dataService = firstData;
    }
    int packetId = -1;
    int appIdent = 0;
    if (s.dataService >= 0 && s.dataService < 4 && s.services[s.dataService].present) {
        packetId = P.Service[s.dataService].DataParam.iPacketID;
        appIdent = P.Service[s.dataService].DataParam.iUserAppIdent;
    }
    P.Unlock();

    // Data services (same thread as process(): no race with the decoder).
    CDataDecoder* dd = rx.GetDataDecoder();
    Slide newSlide;
    bool haveSlide = false;
    if (dd && appIdent == DAB_AT_MOTSLIDESHOW) {
        if (CMOTDABDec* mot = dd->getApplication(packetId)) {
            while (mot->NewObjectAvailable()) {
                CMOTObject obj;
                mot->GetNextObject(obj);
                auto& body = obj.Body.vecData;
                if (body.Size() <= 0)
                    continue;
                newSlide.data.resize(static_cast<std::size_t>(body.Size()));
                for (int i = 0; i < body.Size(); ++i)
                    newSlide.data[static_cast<std::size_t>(i)] = static_cast<uint8_t>(body[i]);
                newSlide.name = obj.strName;
                newSlide.format = obj.strFormat;
                haveSlide = true;
            }
        }
    }
    int jReq = 0;
    {
        std::lock_guard<std::mutex> lk(m);
        jReq = journalineReq;
    }
    if (dd && appIdent == DAB_AT_JOURNALINE) {
        CNews news;
        dd->GetNews(jReq, news);
        if (!news.sTitle.empty() || news.vecItem.Size() > 0) {
            s.journaline.push_back(news.sTitle);
            for (int i = 0; i < news.vecItem.Size(); ++i) {
                const CNewsItem& it = news.vecItem[i];
                if (it.iLink >= 0)
                    s.journaline.push_back("[" + std::to_string(it.iLink) + "] " + it.sText);
                else
                    s.journaline.push_back(it.sText);
            }
        }
    }
    s.journalinePage = jReq;

    std::lock_guard<std::mutex> lk(m);
    if (haveSlide) {
        newSlide.serial = ++slideSerial;
        slide = std::move(newSlide);
    }
    s.slideSerial = slide.serial;
    st = std::move(s);
}

void DrmEngine::Impl::run()
{
    CSettings settingsStore;
    settingsStore.Put("Receiver", "inchansel", int(CS_IQ_POS_ZERO));
    settingsStore.Put("Receiver", "samplerateaud", rate);
    settingsStore.Put("Receiver", "sampleratesig", rate);
    // Dream's DRM chain runs at 48 kHz; a 24 kHz input is upsampled x2.
    settingsStore.Put("Receiver", "sigupratio", rate == 24000 ? 2 : 1);
    CDRMReceiver rx(&settingsStore);
    rx.LoadSettings();
    rx.SetExternalInput(&in);
    rx.SetExternalOutput(&out);
    rx.InitReceiverMode();
    rx.SetInStartMode();
    isRunning = true;

    using clock = std::chrono::steady_clock;
    auto lastRefresh = clock::now();
    int autoDataFor = -1;
    uint64_t slideSerial = 0;
    {
        std::lock_guard<std::mutex> lk(m);
        slideSerial = slide.serial;
        st.running = true;
    }
    while (!stop) {
        applyRequests(rx);
        rx.process();
        const auto now = clock::now();
        if (now - lastRefresh >= std::chrono::milliseconds(200)) {
            lastRefresh = now;
            refresh(rx, autoDataFor, slideSerial);
        }
    }
    isRunning = false;
}

} // namespace aether_drm
