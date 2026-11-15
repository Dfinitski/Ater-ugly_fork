// ANAN fork build 406: this file is MicronSDR's (N7DDC), taken as it is for the
// JS8 sub-mode of FTX in this program; the changes for it are marked "AetherSDR".
// cty.dat, locators: see CtyDat.h
#define _USE_MATH_DEFINES
#include "CtyDat.h"
#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>

CtyDat &CtyDat::instance()
{
    static CtyDat c;
    return c;
}

void CtyDat::load()
{
    loaded = true;
    QFile f(QCoreApplication::applicationDirPath() + "/cty.dat");
#ifdef Q_OS_MACOS
    if(!f.exists())     // AetherSDR, 444: macOS app bundle - Contents/Resources
        f.setFileName(QCoreApplication::applicationDirPath() + "/../Resources/cty.dat");
#endif
    if(!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "cty.dat: not next to the program (no countries)";
        return;
    }
    const QString all = QString::fromLatin1(f.readAll());
    const QStringList recs = all.split(';', Qt::SkipEmptyParts);
    static const QRegularExpression ovr("\\(.*?\\)|\\[.*?\\]|<.*?>|\\{.*?\\}|~.*?~");
    for(const QString &rec : recs)
    {
        const QStringList fl = rec.split(':');
        if(fl.size() < 9)
            continue;
        Entity e;
        e.name = fl[0].trimmed();
        e.cq = fl[1].trimmed().toInt();
        e.itu = fl[2].trimmed().toInt();
        e.cont = fl[3].trimmed();
        e.lat = fl[4].trimmed().toDouble();
        e.lon = -fl[5].trimmed().toDouble();        // cty.dat: west positive
        const int id = ents.size();
        ents.append(e);
        const QStringList ps = fl[8].split(',', Qt::SkipEmptyParts);
        for(QString p : ps)
        {
            p = p.trimmed();
            p.remove(ovr);
            p = p.trimmed().toUpper();
            if(p.isEmpty())
                continue;
            if(p.startsWith('='))
                exact.insert(p.mid(1), id);
            else if(!pfx.contains(p))
                pfx.insert(p, id);
        }
    }
    qDebug() << "cty.dat:" << ents.size() << "entities," << pfx.size() << "prefixes," << exact.size() << "calls";
}

const CtyDat::Entity *CtyDat::entityOf(const QString &callIn)
{
    if(!loaded)
        load();
    if(ents.isEmpty())
        return 0;
    const QString call = callIn.toUpper();
    auto ex = exact.find(call);
    if(ex != exact.end())
        return &ents[ex.value()];
    QString base = call;
    const QStringList parts = call.split('/', Qt::SkipEmptyParts);
    if(parts.size() >= 2)
    {
        static const QStringList suffix = { "P", "M", "MM", "AM", "QRP", "A", "R", "LH", "J" };
        QString a = parts[0], b = parts[1];
        if(suffix.contains(b) || (b.size() == 1 && b[0].isDigit()))
            base = a;
        else if(suffix.contains(a))
            base = b;
        else
            base = (b.size() < a.size()) ? b : a;       // the prefix part (K1ABC/VE3, VP2E/K1ABC)
    }
    for(int n = qMin(base.size(), 12); n >= 1; n--)
    {
        auto it = pfx.find(base.left(n));
        if(it != pfx.end())
            return &ents[it.value()];
    }
    return 0;
}

bool CtyDat::isCall(const QString &w)
{
    if(w.size() < 3 || w.size() > 12)
        return false;
    bool dig = false, let = false;
    for(QChar c : w)
    {
        if(c.isDigit())
            dig = true;
        else if(c >= 'A' && c <= 'Z')
            let = true;
        else if(c != '/')
            return false;
    }
    return dig && let;
}

bool CtyDat::isGrid4(const QString &w)
{
    return w.size() >= 4 && w.left(4) != "RR73" && w[0] >= 'A' && w[0] <= 'R' && w[1] >= 'A' && w[1] <= 'R'
           && w[2].isDigit() && w[3].isDigit();
}

bool CtyDat::gridToLatLon(const QString &gIn, double &lat, double &lon)
{
    const QString g = gIn.toUpper();
    if(g.size() < 4 || g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R' || !g[2].isDigit() || !g[3].isDigit())
        return false;
    lon = (g[0].unicode() - 'A') * 20.0 - 180.0 + (g[2].unicode() - '0') * 2.0;
    lat = (g[1].unicode() - 'A') * 10.0 - 90.0 + (g[3].unicode() - '0') * 1.0;
    if(g.size() >= 6 && g[4] >= 'A' && g[4] <= 'X' && g[5] >= 'A' && g[5] <= 'X')
    {
        lon += (g[4].unicode() - 'A') * (2.0 / 24.0) + 1.0 / 24.0;
        lat += (g[5].unicode() - 'A') * (1.0 / 24.0) + 0.5 / 24.0;
    }
    else
    {
        lon += 1.0;
        lat += 0.5;
    }
    return true;
}

void CtyDat::distAz(double lat1, double lon1, double lat2, double lon2, double &km, double &az)
{
    const double d2r = M_PI / 180.0;
    const double p1 = lat1 * d2r, p2 = lat2 * d2r, dl = (lon2 - lon1) * d2r;
    const double c = sin(p1) * sin(p2) + cos(p1) * cos(p2) * cos(dl);
    km = 6371.0 * acos(qBound(-1.0, c, 1.0));
    az = atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) / d2r;
    if(az < 0)
        az += 360.0;
}
