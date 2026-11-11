#pragma once

// ANAN port (operator, 09.11): what the DRM receiver reports to the DRM
// applet, independent of the decoder behind it (Dream's receiver core, see
// docs/anan100d-port-journal.md section 198). The decoder fills one of these
// a few times a second; the applet only displays it.

#include <QImage>
#include <QString>
#include <QVector>

namespace AetherSDR {

struct DrmStatus {
    // One status light per stage, in signal order.
    enum class Light { Off, Ok, Error, Absent };

    // Whether a decoder is built in at all, and whether it is running on
    // the selected receiver.
    bool decoderAvailable = false;
    bool running = false;
    QString note;                  // one line for the operator (no signal, ...)

    Light io = Light::Off;         // samples arriving
    Light timeSync = Light::Off;
    Light freqSync = Light::Off;
    Light fac = Light::Off;
    Light sdc = Light::Off;
    Light msc = Light::Off;        // audio / data frames

    double snrDb = 0.0;            // NaN-free: 0 while unknown
    double freqOffsetHz = 0.0;
    QChar robustness;              // 'A'..'E', null while unknown
    double bandwidthKHz = 0.0;     // 4.5 .. 20
    QString interleaver;           // "long" / "short"
    QString mscQam;                // "64-QAM", "16-QAM", ...
    QString sdcQam;

    struct Service {
        bool present = false;
        bool audio = false;        // false = data (slideshow, Journaline, ...)
        quint32 id = 0;
        QString label;
        QString codec;             // "AAC", "xHE-AAC", "Opus", "MOT Slideshow", ...
        double bitrateKbps = 0.0;
        QString language;
        QString country;
        bool decodable = true;     // false: no decoder for this codec here
    };
    QVector<Service> services;     // up to 4, in SDC order
    int selectedService = -1;

    QString textMessage;           // the audio service's text message
    QString stationTime;           // UTC time from the SDC, if sent

    // Data services, shown when the applet is popped out.
    QImage slide;                  // the last MOT slideshow image
    QString slideName;
    QString journaline;            // the Journaline page as plain text
};

} // namespace AetherSDR
