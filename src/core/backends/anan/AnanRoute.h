#pragma once

// Build 221 (operator, 09.11): which local network address a radio answered
// on. A radio found by the discovery broadcast can still be unreachable by
// the direct (unicast) packets of a session when the PC has several network
// adapters, or the radio's static address is outside the adapter's subnet:
// the broadcast goes out on every adapter, the unicast follows the routing
// table. Thetis binds its socket to the adapter the radio was found on; the
// transport tries that too (P2Transport::start) and names the adapter when
// nothing comes back.

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QString>

namespace AetherSDR::anan {

struct LocalRoute {
    QHostAddress local;     // this PC's IPv4 address on that adapter
    int prefix = -1;        // its prefix length (subnet)
    QString adapter;        // the adapter's name, for the log and the message
    [[nodiscard]] bool valid() const { return local.protocol() == QAbstractSocket::IPv4Protocol; }
};

inline bool sameSubnet(const QHostAddress& a, const QHostAddress& b, int prefix)
{
    if (prefix < 0 || prefix > 32)
        return false;
    const quint32 mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    return (a.toIPv4Address() & mask) == (b.toIPv4Address() & mask);
}

// The adapter and address a datagram from `radio` came in on: the reply's
// destination address when the platform reports it, else the receiving
// interface, else the adapter whose subnet holds the radio.
inline LocalRoute localRouteFor(const QHostAddress& radio, const QHostAddress& destination,
                                int interfaceIndex)
{
    const auto ifaces = QNetworkInterface::allInterfaces();
    auto usable = [](const QNetworkInterface& i) {
        const auto f = i.flags();
        return f.testFlag(QNetworkInterface::IsUp) && f.testFlag(QNetworkInterface::IsRunning)
               && !f.testFlag(QNetworkInterface::IsLoopBack);
    };
    auto make = [](const QNetworkInterface& i, const QNetworkAddressEntry& e) {
        return LocalRoute{e.ip(), e.prefixLength(), i.humanReadableName()};
    };
    // 1. The reply was sent to this address.
    if (destination.protocol() == QAbstractSocket::IPv4Protocol
        && destination != QHostAddress(QHostAddress::Broadcast)) {
        for (const auto& i : ifaces)
            for (const auto& e : i.addressEntries())
                if (e.ip() == destination)
                    return make(i, e);
    }
    // 2. It came in on this interface: its address in the radio's subnet, else its first.
    if (interfaceIndex > 0) {
        const QNetworkInterface i = QNetworkInterface::interfaceFromIndex(interfaceIndex);
        if (i.isValid()) {
            const QNetworkAddressEntry* first = nullptr;
            for (const auto& e : i.addressEntries()) {
                if (e.ip().protocol() != QAbstractSocket::IPv4Protocol)
                    continue;
                if (sameSubnet(e.ip(), radio, e.prefixLength()))
                    return make(i, e);
                if (!first)
                    first = &e;
            }
            if (first)
                return make(i, *first);
        }
    }
    // 3. The adapter whose subnet holds the radio.
    for (const auto& i : ifaces) {
        if (!usable(i))
            continue;
        for (const auto& e : i.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol
                && sameSubnet(e.ip(), radio, e.prefixLength()))
                return make(i, e);
    }
    return {};
}

inline LocalRoute localRouteFor(const QNetworkDatagram& dg)
{
    return localRouteFor(dg.senderAddress(), dg.destinationAddress(), dg.interfaceIndex());
}

// The adapter that owns `local` (for a route remembered as an address).
inline LocalRoute routeForLocalAddress(const QHostAddress& local)
{
    for (const auto& i : QNetworkInterface::allInterfaces())
        for (const auto& e : i.addressEntries())
            if (e.ip() == local)
                return LocalRoute{e.ip(), e.prefixLength(), i.humanReadableName()};
    return {};
}

}  // namespace AetherSDR::anan
