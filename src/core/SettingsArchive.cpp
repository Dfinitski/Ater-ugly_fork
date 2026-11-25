#include "SettingsArchive.h"

#include "AppSettings.h"
#include "ForkDataDir.h"
#include "SettingsDatabase.h"
#include "SettingsPaths.h"

#include <QDataStream>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace AetherSDR {
namespace SettingsArchive {

namespace {

constexpr char kMagic[] = "AETHERSETTINGS";
constexpr quint32 kVersion = 1;

// Entry names in the file -> where they live on this computer.
const QString kDbEntry = QStringLiteral("AetherSDR.db");
const QString kMidiEntry = QStringLiteral("midi.settings");
const QString kFtxEntry = QStringLiteral("AetherSDR-FTX/Options.ini");

QString midiPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + forkDataDir() + QStringLiteral("/midi.settings");
}

QString ftxPath()
{
    return QDir::homePath() + QStringLiteral("/AetherSDR-FTX/Options.ini");
}

QString pendingDir()
{
    return SettingsPaths::configDir() + QStringLiteral("/settings-restore-pending");
}

QString targetFor(const QString& entry)
{
    if (entry == kDbEntry)
        return SettingsPaths::databasePath();
    if (entry == kMidiEntry)
        return midiPath();
    if (entry == kFtxEntry)
        return ftxPath();
    return {};
}

bool readFile(const QString& path, QByteArray* out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    *out = f.readAll();
    return true;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(data) != data.size())
        return false;
    return f.commit();
}

// The entries of a settings file; false when it is not one.
bool readArchive(const QString& path, QList<QPair<QString, QByteArray>>* entries,
                 QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot open %1").arg(path);
        return false;
    }
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_0);
    QByteArray magic;
    quint32 version = 0;
    quint32 count = 0;
    in >> magic >> version >> count;
    if (in.status() != QDataStream::Ok || magic != QByteArray(kMagic)) {
        *error = QStringLiteral("%1 is not an AetherSDR settings file.").arg(path);
        return false;
    }
    if (version > kVersion) {
        *error = QStringLiteral("%1 was written by a newer program.").arg(path);
        return false;
    }
    for (quint32 i = 0; i < count && i < 64; ++i) {
        QString name;
        QByteArray data;
        in >> name >> data;
        if (in.status() != QDataStream::Ok) {
            *error = QStringLiteral("%1 is damaged.").arg(path);
            return false;
        }
        entries->append({name, data});
    }
    return true;
}

} // namespace

QString defaultFileName()
{
    return QStringLiteral("aether_settings_%1.%2")
        .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")), fileSuffix());
}

QString fileSuffix()
{
    return QStringLiteral("aethersettings");
}

bool exportTo(const QString& path, QString* error)
{
    QString dummy;
    if (!error)
        error = &dummy;
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        *error = QStringLiteral("Cannot create a temporary directory.");
        return false;
    }
    // Everything changed so far into the database, then a consistent copy.
    const QString snapshot = tmp.filePath(QStringLiteral("snapshot.db"));
    if (!AppSettings::instance().exportSnapshot(snapshot)) {
        *error = QStringLiteral("The settings database could not be copied.");
        return false;
    }
    QList<QPair<QString, QByteArray>> entries;
    QByteArray db;
    if (!readFile(snapshot, &db)) {
        *error = QStringLiteral("The settings database could not be read.");
        return false;
    }
    entries.append({kDbEntry, db});
    for (const QString& name : {kMidiEntry, kFtxEntry}) {
        QByteArray data;
        if (QFileInfo::exists(targetFor(name)) && readFile(targetFor(name), &data))
            entries.append({name, data});
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Cannot write %1").arg(path);
        return false;
    }
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_0);
    out << QByteArray(kMagic) << kVersion << quint32(entries.size());
    for (const auto& e : entries)
        out << e.first << e.second;
    if (out.status() != QDataStream::Ok || !f.commit()) {
        *error = QStringLiteral("Cannot write %1").arg(path);
        return false;
    }
    qInfo() << "SettingsArchive: saved" << entries.size() << "stores to" << path;
    return true;
}

bool stageRestore(const QString& path, QString* error)
{
    QString dummy;
    if (!error)
        error = &dummy;
    QList<QPair<QString, QByteArray>> entries;
    if (!readArchive(path, &entries, error))
        return false;
    bool haveDb = false;
    QTemporaryDir tmp;
    for (const auto& e : entries) {
        if (e.first != kDbEntry)
            continue;
        // The database must open and verify as a settings store.
        const QString probe = tmp.filePath(QStringLiteral("probe.db"));
        if (!tmp.isValid() || !writeFile(probe, e.second)
            || !SettingsDatabase::verifyBackupFile(probe)) {
            *error = QStringLiteral("The settings database in %1 is damaged.").arg(path);
            return false;
        }
        haveDb = true;
    }
    if (!haveDb) {
        *error = QStringLiteral("%1 holds no settings database.").arg(path);
        return false;
    }
    QDir(pendingDir()).removeRecursively();
    for (const auto& e : entries) {
        if (targetFor(e.first).isEmpty())
            continue;   // unknown (a later version's extra store): left out
        const QString staged = pendingDir() + QLatin1Char('/') + e.first;
        if (!writeFile(staged, e.second)) {
            QDir(pendingDir()).removeRecursively();
            *error = QStringLiteral("Cannot stage the restore in %1").arg(pendingDir());
            return false;
        }
    }
    qInfo() << "SettingsArchive: restore from" << path << "staged for the next start";
    return true;
}

bool applyPendingRestore()
{
    const QString dir = pendingDir();
    const QString stagedDb = dir + QLatin1Char('/') + kDbEntry;
    if (!QFileInfo::exists(stagedDb))
        return false;
    const QString dbPath = SettingsPaths::databasePath();
    // The current store is kept as a backup first.
    if (QFileInfo::exists(dbPath)) {
        QDir().mkpath(SettingsPaths::backupsDir());
        const QString stamp = QDateTime::currentDateTimeUtc().toString(
            QStringLiteral("yyyyMMdd-HHmmss"));
        QFile::copy(dbPath, SettingsPaths::backupsDir() + QStringLiteral("/AetherSDR-")
                                + stamp + QStringLiteral("-prerestore.db"));
    }
    bool ok = true;
    for (const QString& name : {kDbEntry, kMidiEntry, kFtxEntry}) {
        const QString staged = dir + QLatin1Char('/') + name;
        if (!QFileInfo::exists(staged))
            continue;
        const QString target = targetFor(name);
        if (name == kDbEntry) {
            QFile::remove(target + QStringLiteral("-wal"));
            QFile::remove(target + QStringLiteral("-shm"));
        }
        QByteArray data;
        if (!readFile(staged, &data) || !writeFile(target, data)) {
            ok = false;
            qWarning() << "SettingsArchive: could not restore" << target;
        }
    }
    QDir(dir).removeRecursively();
    qInfo() << "SettingsArchive: staged settings restored" << (ok ? "" : "(with errors)");
    return true;
}

} // namespace SettingsArchive
} // namespace AetherSDR
