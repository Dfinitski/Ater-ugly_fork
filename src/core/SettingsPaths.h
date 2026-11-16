#pragma once

#include <QString>

namespace AetherSDR {

// Single source of truth for every settings-store path. Backed by
// QStandardPaths::writableLocation(GenericConfigLocation), which needs no
// QCoreApplication and honours setTestModeEnabled() (tests/TestSettingsProfile.h).
namespace SettingsPaths {

// ~/.config/AetherSDR_ugly_fork (Linux), ~/Library/Preferences/AetherSDR_ugly_fork
// (macOS), %LOCALAPPDATA%/AetherSDR_ugly_fork (Windows) -- the ANAN fork's own
// directory since build 449 (core/ForkDataDir.h). Does NOT create the directory.
QString configDir();

// Build 449: on the first start of a fork build with no directory of its own,
// copy the user's settings and data from the original program's directory
// (<GenericConfigLocation>/AetherSDR) -- the settings database and its WAL,
// settings backups, themes, presets, models, FFTW wisdom and the like; not
// logs, caches, spot logs, firmware, quarantine or lock files. A copy: the
// original program keeps its own directory untouched. No-op with an explicit
// AETHER_SETTINGS_DIR (an isolated profile) or when the fork's directory
// exists. Call before anything opens the settings store.
void copyFromUpstreamOnce();

// The SQLite settings store: <configDir>/AetherSDR.db
QString databasePath();

// The pre-SQLite XML store (now a frozen snapshot after migration):
// <configDir>/AetherSDR.settings
QString legacyXmlPath();

// Rolling verified backups + pre-reset backups: <configDir>/settings-backups
QString backupsDir();

// Quarantined corrupt stores: <configDir>/settings-quarantine
QString quarantineDir();

} // namespace SettingsPaths

} // namespace AetherSDR
