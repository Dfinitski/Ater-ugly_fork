#pragma once

// ANAN fork build 691 (operator, 24.11: "Settings: Save Settings and Restore
// Settings above Reset Settings -- a file dialog to save under a chosen name
// and to restore from it; everything the program keeps must go into the
// file; default name aether_settings_<date>").
//
// One file holds every settings store of the program:
//   - the settings database (AetherSDR.db, a consistent VACUUM INTO snapshot);
//   - midi.settings (the MIDI bindings), if present;
//   - the FTX / JS8 windows' Options.ini (~/AetherSDR-FTX), if present.
// (Nothing of this program goes to the Windows registry; credentials stay in
// the system's credential store and are not part of a settings file.)
//
// The file: *.ats (build 692). Format: QDataStream -- magic "AETHERSETTINGS", version, then (name, bytes)
// entries. A restore is staged into <configDir>/settings-restore-pending and
// applied by the next start, before anything opens a store, so nothing the
// closing program writes can overwrite it.

#include <QString>

namespace AetherSDR {
namespace SettingsArchive {

// The file name suggested by Save Settings: aether_settings_<yyyy-MM-dd>.
QString defaultFileName();
// The extension of a settings file, without the dot.
QString fileSuffix();

// Writes the current settings to `path`. On failure returns false and an
// explanation in `error`.
bool exportTo(const QString& path, QString* error);

// Reads and checks `path`, and stages it for the next start. On failure
// returns false and an explanation in `error`; nothing is staged then.
bool stageRestore(const QString& path, QString* error);

// At start, before the settings store is opened: puts a staged restore in
// place (the current database is kept in settings-backups as *-prerestore.db
// first). Returns true when something was restored.
bool applyPendingRestore();

} // namespace SettingsArchive
} // namespace AetherSDR
