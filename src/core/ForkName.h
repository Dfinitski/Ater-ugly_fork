#pragma once

// ANAN fork build 402 (operator, 14.11: "the program's version must not be
// confused with the real Aether: 'Aether ugly fork' and the build number; the
// original name is shown nowhere but in About") -- corrected by the operator
// in build 403: "everything that goes OUT must be named so; inside the
// program and its interface it stays as it was". So: the name this fork SENDS
// -- FreeDV Reporter, PSK Reporter, wsprnet, spot comments. Not window titles,
// not messages. Defined in
// gui/MainWindow_Menus.cpp, which has the build number (ForkBuild.h), so that
// the files using it are not rebuilt with every build.

#include <QString>

namespace AetherSDR {

QString forkDisplayName();      // "Aether ugly fork"
QString forkDisplayVersion();   // "Aether ugly fork 402"

}  // namespace AetherSDR
