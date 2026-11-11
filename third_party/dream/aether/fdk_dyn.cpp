/* AetherSDR: FDK-AAC loaded at run time.
 *
 * Dream's FdkAacCodec (src/sourcedecoders/fdk_aac_codec.cpp) calls the FDK-AAC
 * API directly. AetherSDR does not link FDK-AAC: this file defines those
 * functions and forwards them to a library found the first time one is called:
 *   Windows: fdk-aac.dll, libfdk-aac-2.dll, libfdk-aac.dll (the program's
 *            directory first: LoadLibrary's own search order)
 *   Linux:   libfdk-aac.so.2, libfdk-aac.so -- the executable's ../lib (the
 *            AppImage's usr/lib) and its own directory, then the system
 *   macOS:   libfdk-aac.2.dylib, libfdk-aac.dylib
 * Without it every call fails cleanly: aacDecoder_GetLibInfo reports no
 * modules, so FdkAacCodec::CanDecode() is false and DRM runs without audio
 * for AAC / xHE-AAC services (sync, labels, text and data still work).
 *
 * The FDK-AAC headers (third_party/fdk-aac) are used for the types only.
 * GPL v2 or later, like the rest of third_party/dream. */

#include <fdk-aac/aacdecoder_lib.h>
#include <fdk-aac/aacenc_lib.h>

#include <mutex>

#ifdef _WIN32
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#else
# include <dlfcn.h>
# include <string>
# include <unistd.h>
#endif

namespace {

struct FdkApi {
    HANDLE_AACDECODER (*decOpen)(TRANSPORT_TYPE, UINT) = nullptr;
    AAC_DECODER_ERROR (*decConfigRaw)(HANDLE_AACDECODER, UCHAR*[], const UINT[]) = nullptr;
    AAC_DECODER_ERROR (*decFill)(HANDLE_AACDECODER, UCHAR*[], const UINT[], UINT*) = nullptr;
    AAC_DECODER_ERROR (*decDecodeFrame)(HANDLE_AACDECODER, INT_PCM*, const INT, const UINT) = nullptr;
    void (*decClose)(HANDLE_AACDECODER) = nullptr;
    CStreamInfo* (*decGetStreamInfo)(HANDLE_AACDECODER) = nullptr;
    INT (*decGetLibInfo)(LIB_INFO*) = nullptr;
    AACENC_ERROR (*encOpen)(HANDLE_AACENCODER*, const UINT, const UINT) = nullptr;
    AACENC_ERROR (*encClose)(HANDLE_AACENCODER*) = nullptr;
    AACENC_ERROR (*encEncode)(const HANDLE_AACENCODER, const AACENC_BufDesc*, const AACENC_BufDesc*,
                              const AACENC_InArgs*, AACENC_OutArgs*) = nullptr;
    AACENC_ERROR (*encInfo)(const HANDLE_AACENCODER, AACENC_InfoStruct*) = nullptr;
    AACENC_ERROR (*encSetParam)(const HANDLE_AACENCODER, const AACENC_PARAM, const UINT) = nullptr;
    bool decoder = false;   // the decoder entry points all resolved
    bool encoder = false;
};

void* openLibrary()
{
#ifdef _WIN32
    const char* const names[] = {"fdk-aac.dll", "libfdk-aac-2.dll", "libfdk-aac.dll"};
    for (const char* n : names)
        if (HMODULE h = LoadLibraryA(n))
            return reinterpret_cast<void*>(h);
#else
# ifdef __APPLE__
    const char* const names[] = {"libfdk-aac.2.dylib", "libfdk-aac.dylib"};
# else
    const char* const names[] = {"libfdk-aac.so.2", "libfdk-aac.so"};
    // The bundled copy first (AppImage: usr/bin/AetherSDR + usr/lib/...):
    // dlopen() of a bare name does not look next to the program.
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = '\0';
        std::string dir(exe);
        dir.erase(dir.find_last_of('/') + 1);
        for (const char* sub : {"../lib/", ""})
            for (const char* name : names)
                if (void* h = dlopen((dir + sub + name).c_str(), RTLD_NOW | RTLD_LOCAL))
                    return h;
    }
# endif
    for (const char* n : names)
        if (void* h = dlopen(n, RTLD_NOW | RTLD_LOCAL))
            return h;
#endif
    return nullptr;
}

void* symbol(void* lib, const char* name)
{
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

template <typename F>
bool resolve(void* lib, F& fn, const char* name)
{
    fn = reinterpret_cast<F>(symbol(lib, name));
    return fn != nullptr;
}

const FdkApi& api()
{
    static FdkApi a;
    static std::once_flag once;
    std::call_once(once, [] {
        void* lib = openLibrary();
        if (!lib)
            return;
        bool d = true;
        d &= resolve(lib, a.decOpen, "aacDecoder_Open");
        d &= resolve(lib, a.decConfigRaw, "aacDecoder_ConfigRaw");
        d &= resolve(lib, a.decFill, "aacDecoder_Fill");
        d &= resolve(lib, a.decDecodeFrame, "aacDecoder_DecodeFrame");
        d &= resolve(lib, a.decClose, "aacDecoder_Close");
        d &= resolve(lib, a.decGetStreamInfo, "aacDecoder_GetStreamInfo");
        d &= resolve(lib, a.decGetLibInfo, "aacDecoder_GetLibInfo");
        a.decoder = d;
        bool e = true;
        e &= resolve(lib, a.encOpen, "aacEncOpen");
        e &= resolve(lib, a.encClose, "aacEncClose");
        e &= resolve(lib, a.encEncode, "aacEncEncode");
        e &= resolve(lib, a.encInfo, "aacEncInfo");
        e &= resolve(lib, a.encSetParam, "aacEncoder_SetParam");
        a.encoder = e;
        // The library stays loaded for the life of the process.
    });
    return a;
}

} // namespace

extern "C" {

bool aether_fdk_aac_available()
{
    return api().decoder;
}

HANDLE_AACDECODER aacDecoder_Open(TRANSPORT_TYPE transportFmt, UINT nrOfLayers)
{
    return api().decoder ? api().decOpen(transportFmt, nrOfLayers) : nullptr;
}

AAC_DECODER_ERROR aacDecoder_ConfigRaw(HANDLE_AACDECODER self, UCHAR* conf[], const UINT length[])
{
    return api().decoder ? api().decConfigRaw(self, conf, length) : AAC_DEC_INVALID_HANDLE;
}

AAC_DECODER_ERROR aacDecoder_Fill(HANDLE_AACDECODER self, UCHAR* pBuffer[], const UINT bufferSize[],
                                  UINT* bytesValid)
{
    return api().decoder ? api().decFill(self, pBuffer, bufferSize, bytesValid) : AAC_DEC_INVALID_HANDLE;
}

AAC_DECODER_ERROR aacDecoder_DecodeFrame(HANDLE_AACDECODER self, INT_PCM* pTimeData,
                                         const INT timeDataSize, const UINT flags)
{
    return api().decoder ? api().decDecodeFrame(self, pTimeData, timeDataSize, flags)
                         : AAC_DEC_INVALID_HANDLE;
}

void aacDecoder_Close(HANDLE_AACDECODER self)
{
    if (api().decoder && self)
        api().decClose(self);
}

CStreamInfo* aacDecoder_GetStreamInfo(HANDLE_AACDECODER self)
{
    return api().decoder ? api().decGetStreamInfo(self) : nullptr;
}

INT aacDecoder_GetLibInfo(LIB_INFO* info)
{
    // Caller zeroes the array; without the library it stays "no modules".
    return api().decoder ? api().decGetLibInfo(info) : -1;
}

AACENC_ERROR aacEncOpen(HANDLE_AACENCODER* phAacEncoder, const UINT encModules, const UINT maxChannels)
{
    if (!api().encoder) {
        if (phAacEncoder)
            *phAacEncoder = nullptr;
        return AACENC_INVALID_HANDLE;
    }
    return api().encOpen(phAacEncoder, encModules, maxChannels);
}

AACENC_ERROR aacEncClose(HANDLE_AACENCODER* phAacEncoder)
{
    return api().encoder ? api().encClose(phAacEncoder) : AACENC_INVALID_HANDLE;
}

AACENC_ERROR aacEncEncode(const HANDLE_AACENCODER hAacEncoder, const AACENC_BufDesc* inBufDesc,
                          const AACENC_BufDesc* outBufDesc, const AACENC_InArgs* inargs,
                          AACENC_OutArgs* outargs)
{
    return api().encoder ? api().encEncode(hAacEncoder, inBufDesc, outBufDesc, inargs, outargs)
                         : AACENC_INVALID_HANDLE;
}

AACENC_ERROR aacEncInfo(const HANDLE_AACENCODER hAacEncoder, AACENC_InfoStruct* pInfo)
{
    return api().encoder ? api().encInfo(hAacEncoder, pInfo) : AACENC_INVALID_HANDLE;
}

AACENC_ERROR aacEncoder_SetParam(const HANDLE_AACENCODER hAacEncoder, const AACENC_PARAM param,
                                 const UINT value)
{
    return api().encoder ? api().encSetParam(hAacEncoder, param, value) : AACENC_INVALID_HANDLE;
}

} // extern "C"
