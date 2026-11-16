#include "SettingsPaths.h"
#include "core/ForkDataDir.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

namespace AetherSDR {
namespace SettingsPaths {

QString configDir()
{
    // Explicit override, primarily for test isolation: on Windows,
    // QStandardPaths resolves known folders through the shell API and ignores
    // environment redirects, so QStandardPaths::setTestModeEnabled() lands
    // every test run in the same real %LOCALAPPDATA%\qttest directory
    // (PR #4612 review, nigelfenton — demonstrated flaky there, clean on
    // POSIX). TestSettingsProfile sets this to its per-run temp dir; it also
    // makes the --config CLI drivable against an isolated store.
    const QString override = qEnvironmentVariable("AETHER_SETTINGS_DIR");
    if (!override.isEmpty()) {
        return override;
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + AetherSDR::forkDataDir();
}

QString databasePath()
{
    return configDir() + QStringLiteral("/AetherSDR.db");
}

QString legacyXmlPath()
{
    return configDir() + QStringLiteral("/AetherSDR.settings");
}

QString backupsDir()
{
    return configDir() + QStringLiteral("/settings-backups");
}

QString quarantineDir()
{
    return configDir() + QStringLiteral("/settings-quarantine");
}

void copyFromUpstreamOnce()
{
    if (!qEnvironmentVariableIsEmpty("AETHER_SETTINGS_DIR"))
        return;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString target = base + AetherSDR::forkDataDir();
    const QString source = base + AetherSDR::upstreamDataDir();
    if (base.isEmpty() || QFileInfo::exists(target) || !QFileInfo::exists(source))
        return;
    if (!QFileInfo::exists(source + QStringLiteral("/AetherSDR.db"))
        && !QFileInfo::exists(source + QStringLiteral("/AetherSDR.settings")))
        return;   // no settings to carry over: start fresh
    // Top-level entries that are not settings (or belong to one running
    // program): logs, caches (the nested "AetherSDR" is Qt's AppData of the
    // original), spot-source logs, staged firmware, quarantined stores.
    static const QStringList kSkipTop = {
        QStringLiteral("logs"), QStringLiteral("cache"), QStringLiteral("spothub"),
        QStringLiteral("firmware"), QStringLiteral("settings-quarantine"),
        QStringLiteral("AetherSDR"),
    };
    if (!QDir().mkpath(target)) {
        qWarning() << "Fork data dir: cannot create" << target;
        return;
    }
    int copied = 0, failed = 0;
    QDirIterator it(source, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    const QDir src(source);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = src.relativeFilePath(path);
        const QString top = rel.section(QLatin1Char('/'), 0, 0);
        if (rel.contains(QLatin1Char('/')) && kSkipTop.contains(top))
            continue;
        // One program's lock and shared-memory index: never copied (SQLite
        // rebuilds the -shm; the -wal holds committed pages and is copied).
        if (rel.endsWith(QStringLiteral(".lock")) || rel.endsWith(QStringLiteral("-shm")))
            continue;
        if (!rel.contains(QLatin1Char('/')) && rel.endsWith(QStringLiteral(".log")))
            continue;   // a log left at the root by an old version
        const QString dst = target + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (QFile::copy(path, dst))
            ++copied;
        else
            ++failed;
    }
    qInfo().noquote() << "Fork data dir: copied" << copied << "files from" << source << "to"
                      << target << (failed ? QStringLiteral("(%1 failed)").arg(failed) : QString());
}

} // namespace SettingsPaths
} // namespace AetherSDR
