#pragma once

// ANAN-100D port (06.11.2026, first users: "the program closed unexpectedly
// when the radio was switched on again"). Nothing recorded a crash: the log
// is written asynchronously, so its last lines die with the process, and
// Windows only offers its own error report. This installs, on Windows, a
// last-chance handler that writes next to the logs:
//   aethersdr-crash-<date>-<time>.txt  exception, address, module + offset,
//                                      thread, fork build, the session's log
//   aethersdr-crash-<date>-<time>.dmp  a minidump (threads, stacks, the
//                                      memory they point at)
// and then gives the log writer up to 2 s to put its queue on disk. Covers
// unhandled SEH exceptions (access violations ...), abort() (qFatal, failed
// asserts, std::terminate) and the CRT's invalid-parameter / pure-call traps.
// The .dmp opens in Visual Studio / WinDbg with the build's AetherSDR.pdb.
// Header-only so no build file changes; dbghelp is loaded at crash time.

#ifdef _WIN32

#include "core/ForkBuild.h"
#include "core/LogManager.h"

#include <QString>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>

namespace AetherSDR::CrashDump {

namespace detail {

inline wchar_t g_dir[MAX_PATH] = {};
inline wchar_t g_logPath[MAX_PATH] = {};
inline volatile LONG g_once = 0;

inline DWORD WINAPI flushThread(LPVOID)
{
    AetherSDR::LogManager::instance().flushLog();
    return 0;
}

inline void write(EXCEPTION_POINTERS* ep, const char* what)
{
    // One report per process, whichever trap fires first.
    if (InterlockedExchange(&g_once, 1) != 0)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t base[MAX_PATH];
    _snwprintf_s(base, MAX_PATH, _TRUNCATE, L"%s\\aethersdr-crash-%04u%02u%02u-%02u%02u%02u", g_dir,
                 t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);

    // Where it happened: module and offset, readable without symbols.
    void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    wchar_t module[MAX_PATH] = L"?";
    unsigned long long offset = 0;
    HMODULE mod = nullptr;
    if (addr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                       | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCWSTR>(addr), &mod)) {
        GetModuleFileNameW(mod, module, MAX_PATH);
        offset = reinterpret_cast<unsigned long long>(addr) - reinterpret_cast<unsigned long long>(mod);
    }

    wchar_t txt[MAX_PATH];
    _snwprintf_s(txt, MAX_PATH, _TRUNCATE, L"%s.txt", base);
    FILE* f = nullptr;
    if (_wfopen_s(&f, txt, L"w") == 0 && f) {
        std::fprintf(f, "AetherSDR ANAN fork build %d crashed: %s\n", AETHER_FORK_BUILD, what);
        if (ep && ep->ExceptionRecord) {
            std::fprintf(f, "exception code 0x%08lX at %p\n",
                         static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode), addr);
            if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
                && ep->ExceptionRecord->NumberParameters >= 2)
                std::fprintf(f, "access violation: %s address 0x%llX\n",
                             ep->ExceptionRecord->ExceptionInformation[0] == 0   ? "reading"
                             : ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing"
                                                                                 : "executing",
                             static_cast<unsigned long long>(ep->ExceptionRecord->ExceptionInformation[1]));
            std::fprintf(f, "module %ls + 0x%llX\n", module, offset);
        }
        std::fprintf(f, "thread %lu\n", static_cast<unsigned long>(GetCurrentThreadId()));
        std::fprintf(f, "log %ls\n", g_logPath);
        std::fprintf(f, "dump %ls.dmp\n", base);
        std::fclose(f);
    }

    // The minidump.
    if (HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll")) {
        using WriteFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                      PMINIDUMP_EXCEPTION_INFORMATION,
                                      PMINIDUMP_USER_STREAM_INFORMATION,
                                      PMINIDUMP_CALLBACK_INFORMATION);
        auto fn = reinterpret_cast<WriteFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
        wchar_t dmp[MAX_PATH];
        _snwprintf_s(dmp, MAX_PATH, _TRUNCATE, L"%s.dmp", base);
        HANDLE h = CreateFileW(dmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fn && h != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            fn(GetCurrentProcess(), GetCurrentProcessId(), h,
               static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory
                                          | MiniDumpWithUnloadedModules),
               ep ? &mei : nullptr, nullptr, nullptr);
        }
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }

    // The log's queue, at most 2 s: the writer may be the thread that died
    // or wait on a lock it held.
    if (HANDLE th = CreateThread(nullptr, 0, &flushThread, nullptr, 0, nullptr)) {
        WaitForSingleObject(th, 2000);
        CloseHandle(th);
    }
}

inline LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* ep)
{
    write(ep, "unhandled exception");
    return EXCEPTION_CONTINUE_SEARCH;   // Windows' own report still follows
}

// abort() and friends have no EXCEPTION_POINTERS of their own: raise one so
// the dump carries the calling thread's context.
inline void reportHere(const char* what)
{
    __try {
        RaiseException(0xE0000ABC, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    } __except (write(GetExceptionInformation(), what), EXCEPTION_EXECUTE_HANDLER) {
    }
}

inline void onAbort(int)
{
    reportHere("abort() (qFatal, a failed assert or std::terminate)");
}

inline void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    reportHere("CRT invalid parameter");
    std::abort();
}

inline void onPureCall()
{
    reportHere("pure virtual call");
    std::abort();
}

}  // namespace detail

// Call once, as early as the log directory is known.
inline void install(const QString& logDir, const QString& logPath)
{
    wcsncpy_s(detail::g_dir, QString(logDir).replace(QLatin1Char('/'), QLatin1Char('\\')).toStdWString().c_str(),
              _TRUNCATE);
    wcsncpy_s(detail::g_logPath,
              QString(logPath).replace(QLatin1Char('/'), QLatin1Char('\\')).toStdWString().c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(&detail::unhandledFilter);
    std::signal(SIGABRT, &detail::onAbort);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(&detail::onInvalidParameter);
    _set_purecall_handler(&detail::onPureCall);
    std::set_terminate([] { std::abort(); });
}

}  // namespace AetherSDR::CrashDump

#else

#include <QString>

namespace AetherSDR::CrashDump {
inline void install(const QString&, const QString&) {}
}  // namespace AetherSDR::CrashDump

#endif
