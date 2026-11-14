#pragma once

// ANAN fork build 402 (operator, 14.11: "the program's version must not be
// confused with the real Aether: 'Aether ugly fork' and the build number; the
// original name is shown nowhere but in About"). The name this fork shows and
// sends -- window titles, FreeDV Reporter, PSK Reporter, wsprnet. Defined in
// gui/MainWindow_Menus.cpp, which has the build number (ForkBuild.h), so that
// the files using it are not rebuilt with every build.

#include <QString>

namespace AetherSDR {

QString forkDisplayName();      // "Aether ugly fork"
QString forkDisplayVersion();   // "Aether ugly fork 402"

}  // namespace AetherSDR
