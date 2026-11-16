// AetherSDR ANAN fork ("AetherSDR_ugly_fork"), build 449: the fork's own name
// for its data, so that it and the original AetherSDR can be installed and
// run on one computer without sharing (and spoiling) each other's settings
// database, logs, themes, presets and caches.
//
// Every per-user directory the program used as <GenericConfigLocation>/AetherSDR
// is <GenericConfigLocation>/AetherSDR_ugly_fork in the fork, and the Qt
// application / organization name is "AetherSDR_ugly_fork" (so the
// AppData / AppConfig / cache locations differ as well). The first start of a
// fork build that has no directory of its own copies the user's settings from
// the AetherSDR directory (SettingsPaths::copyFromUpstreamOnce()).
#pragma once

#include <QString>

namespace AetherSDR {

inline constexpr const char* kForkAppName = "AetherSDR_ugly_fork";

// "/AetherSDR_ugly_fork": appended to a QStandardPaths location.
inline QString forkDataDir()
{
    return QStringLiteral("/AetherSDR_ugly_fork");
}

// The original program's directory name under the same locations.
inline QString upstreamDataDir()
{
    return QStringLiteral("/AetherSDR");
}

} // namespace AetherSDR
