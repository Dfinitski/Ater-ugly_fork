// AetherSDR ANAN fork, build 446: the files the FTX / JS8 / FreeDV modes load
// at run time (MicronSDR's modules and cty.dat), where the program expects
// them, and texts that say what is wrong when one is missing or refused.
//
// Windows: next to AetherSDR.exe (the portable archive). Linux: next to the
// executable (the AppImage's usr/bin, or the build directory). macOS: the app
// bundle's Contents/Frameworks (libraries) and Contents/Resources (cty.dat).
#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

namespace ModuleFiles {

struct Entry {
    QString file;      // file name, e.g. "libftx.dll"
    QString dir;       // the directory it is expected in
    QString feature;   // what needs it, e.g. "FT8 / FT4 / WSPR"
};

inline QString appDir() { return QCoreApplication::applicationDirPath(); }

// Where the modules (libraries) are expected.
inline QString libDir()
{
#ifdef Q_OS_MACOS
    return QDir::cleanPath(appDir() + QStringLiteral("/../Frameworks"));
#else
    return appDir();
#endif
}

// Where cty.dat is expected.
inline QString dataDir()
{
#ifdef Q_OS_MACOS
    return QDir::cleanPath(appDir() + QStringLiteral("/../Resources"));
#else
    return appDir();
#endif
}

inline QList<Entry> required()
{
#if defined(Q_OS_WIN)
    const QString ftx = QStringLiteral("libftx.dll"), js8 = QStringLiteral("libjs8.dll"),
                  fdv = QStringLiteral("libfdv.dll"), c2 = QStringLiteral("libcodec2.dll");
#elif defined(Q_OS_MACOS)
    const QString ftx = QStringLiteral("libftx.dylib"), js8 = QStringLiteral("libjs8.dylib"),
                  fdv = QStringLiteral("libfdv.dylib"), c2 = QStringLiteral("libcodec2.1.2.dylib");
#else
    const QString ftx = QStringLiteral("libftx.so"), js8 = QStringLiteral("libjs8.so"),
                  fdv = QStringLiteral("libfdv.so"), c2 = QStringLiteral("libcodec2.so.1.2");
#endif
    return {
        {ftx, libDir(), QStringLiteral("FT8 / FT4 / WSPR")},
        {js8, libDir(), QStringLiteral("JS8")},
        {fdv, libDir(), QStringLiteral("FreeDV")},
        {c2, libDir(), QStringLiteral("FreeDV")},
        {QStringLiteral("cty.dat"), dataDir(), QStringLiteral("FTX countries")},
    };
}

inline QList<Entry> missing()
{
    QList<Entry> out;
    for (const Entry& e : required())
        if (!QFileInfo::exists(QDir(e.dir).filePath(e.file)))
            out.append(e);
    return out;
}

// What to do about a missing file, for the platform.
inline QString missingAdvice()
{
#if defined(Q_OS_WIN)
    return QStringLiteral(
        "Unpack the whole release archive into one folder (not only AetherSDR.exe, and not by "
        "running the program from inside the ZIP). If the file was there, an antivirus may have "
        "removed it: look in its quarantine, restore the file and add the folder to its "
        "exclusions.");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Copy AetherSDR.app from the release DMG again.");
#else
    return QStringLiteral(
        "Use the release AppImage, or copy MicronSDR's Linux modules (third_party/*/linux64) "
        "next to the program.");
#endif
}

// The text for a library that did not load: missing, or there but refused.
// file: the platform file name (e.g. "libftx.dll"); systemError: QLibrary's
// errorString() of the load next to the program.
inline QString loadFailureText(const QString& file, const QString& systemError)
{
    const QString dir = QDir::toNativeSeparators(libDir());
    if (!QFileInfo::exists(QDir(libDir()).filePath(file)))
        return QStringLiteral("%1 is missing in %2. %3").arg(file, dir, missingAdvice());
    return QStringLiteral("%1 is in %2 but the system refused to load it (%3). An antivirus "
                          "may be blocking it: look in its quarantine and add the folder to its "
                          "exclusions.")
        .arg(file, dir, systemError);
}

// Platform file name of a module by its base name ("libftx" -> "libftx.dll").
inline QString fileName(const QString& base)
{
    for (const Entry& e : required())
        if (e.file.startsWith(base + QLatin1Char('.')))
            return e.file;
    return base;
}

} // namespace ModuleFiles
