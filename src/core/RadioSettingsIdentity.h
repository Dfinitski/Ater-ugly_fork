#pragma once

#include <QString>

namespace AetherSDR {

// Discovery's connection locator and the USB-reported identity are different
// facts. A duplicate serial can need an index locator while still reporting a
// genuine serial. Empty/unknown reported identity deliberately shares RTL's
// family row; an enumeration index never establishes persistent identity.
struct RadioSerialIdentity {
    QString reportedSerial;
    bool indexLocator = false;
};

inline QString settingsRadioId(const QString& family, const QString& locator,
                               const RadioSerialIdentity& identity)
{
    // ANAN-100D port: only RTL-SDR keyed settings on the device-reported serial.
    Q_UNUSED(family);
    Q_UNUSED(identity);
    return locator;
}

} // namespace AetherSDR
