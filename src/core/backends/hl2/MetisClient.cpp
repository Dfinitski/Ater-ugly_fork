// The radio transport interface of Hl2Backend (see MetisClient.h). The
// implementation is anan::P2Transport; this file only anchors the class for
// moc (AUTOMOC pairs MetisClient.h with this source).
#include "core/backends/hl2/MetisClient.h"

namespace AetherSDR::hl2 {

MetisClient::MetisClient(QObject* parent) : QObject(parent)
{
    // These payloads cross from the I/O thread to the GUI thread queued;
    // registered explicitly (as the Protocol 1 client did) so a name-based
    // or queued delivery never drops them.
    qRegisterMetaType<AetherSDR::hl2::Hl2Telemetry>("AetherSDR::hl2::Hl2Telemetry");
    qRegisterMetaType<AetherSDR::hl2::MetisClient::LinkCounters>(
        "AetherSDR::hl2::MetisClient::LinkCounters");
    qRegisterMetaType<AetherSDR::hl2::Ep4Stats>("AetherSDR::hl2::Ep4Stats");
    qRegisterMetaType<QList<float>>("QList<float>");
}

}  // namespace AetherSDR::hl2
