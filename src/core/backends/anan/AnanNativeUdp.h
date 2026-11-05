#pragma once

// ANAN-100D port (operator, 01.11: "sending packets to the radio is the most
// time-critical task; the transmitter and the speaker each in their own
// thread at the highest priority, synchronised with the device").
//
// QUdpSocket may only be used on its own thread. The DUC (transmit IQ) and
// codec (speaker) packets are sent from their own threads, so they go out
// through the operating system's sendto() on the session socket's native
// descriptor: same local port (the radio answers the port the session came
// from), no Qt object touched. sendto() on one socket from several threads is
// safe on Windows and POSIX.

#include <QtGlobal>

#include <cstddef>

namespace AetherSDR::anan {

// ipv4: host byte order (QHostAddress::toIPv4Address()). Returns the bytes
// sent, or -1.
qint64 nativeUdpSendTo(qintptr fd, quint32 ipv4, quint16 port, const void* data, std::size_t len) noexcept;

// Raises the calling thread to the highest scheduling priority the platform
// grants a normal process (Windows: THREAD_PRIORITY_TIME_CRITICAL plus the
// MMCSS "Pro Audio" class when available). Returns false if nothing changed.
bool raiseCurrentThreadToTimeCritical() noexcept;

}  // namespace AetherSDR::anan
