#include "core/backends/anan/AnanNativeUdp.h"

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <pthread.h>
#  include <sched.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#endif

#include <cstring>

namespace AetherSDR::anan {

qint64 nativeUdpSendTo(qintptr fd, quint32 ipv4, quint16 port, const void* data, std::size_t len) noexcept
{
    if (fd < 0 || !data || len == 0)
        return -1;
    sockaddr_in to;
    std::memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(ipv4);
#ifdef Q_OS_WIN
    const int r = ::sendto(static_cast<SOCKET>(fd), static_cast<const char*>(data), static_cast<int>(len), 0,
                           reinterpret_cast<const sockaddr*>(&to), static_cast<int>(sizeof to));
    return r == SOCKET_ERROR ? -1 : static_cast<qint64>(r);
#else
    const ssize_t r = ::sendto(static_cast<int>(fd), data, len, 0, reinterpret_cast<const sockaddr*>(&to),
                               sizeof to);
    return r < 0 ? -1 : static_cast<qint64>(r);
#endif
}

bool raiseCurrentThreadToTimeCritical() noexcept
{
#ifdef Q_OS_WIN
    // MMCSS "Pro Audio" (avrt.dll, loaded at run time: no link dependency).
    // The scheduler then keeps the thread above normal work even under load,
    // as audio drivers do; TIME_CRITICAL alone is the fallback.
    using AvSetFn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
    if (HMODULE avrt = ::LoadLibraryW(L"avrt.dll")) {
        if (auto fn = reinterpret_cast<AvSetFn>(::GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"))) {
            DWORD task = 0;
            fn(L"Pro Audio", &task);
        }
    }
    return ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
#elif defined(Q_OS_MACOS)
    return false;   // macOS: the QoS class set by startStreamThread() governs (AppActivity.h)
#else
    // Real-time scheduling needs privileges on Linux; ask, accept a refusal.
    sched_param sp;
    std::memset(&sp, 0, sizeof sp);
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
    return pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) == 0;
#endif
}

}  // namespace AetherSDR::anan
