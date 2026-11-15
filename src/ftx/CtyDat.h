// ANAN fork build 406: this file is MicronSDR's (N7DDC), taken as it is for the
// JS8 sub-mode of FTX in this program; the changes for it are marked "AetherSDR".
/*
 * CtyDat - the countries of the callsigns (cty.dat of AD1C, next to the program), the locators,
 * distance and azimuth. One copy for the program (CtyDat::instance(), loaded on the first use).
 */
#ifndef CTYDAT_H
#define CTYDAT_H

#include <QString>
#include <QVector>
#include <QHash>

class CtyDat
{
public:
    struct Entity { QString name, cont; int cq, itu; double lat, lon; };

    static CtyDat &instance();
    const Entity *entityOf(const QString &call);        // 0: unknown

    static bool isCall(const QString &w);
    static bool isGrid4(const QString &w);
    static bool gridToLatLon(const QString &g, double &lat, double &lon);
    static void distAz(double lat1, double lon1, double lat2, double lon2, double &km, double &az);

private:
    CtyDat() : loaded(false) {}
    void load();
    bool loaded;
    QVector<Entity> ents;
    QHash<QString, int> pfx;           // prefix -> entity
    QHash<QString, int> exact;         // =CALL -> entity
};

#endif // CTYDAT_H
