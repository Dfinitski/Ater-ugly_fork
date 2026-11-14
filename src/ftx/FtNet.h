// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
/*
 * FtNet - the FTX window and the internet (besides PSK Reporter, PskReporter.h) and its log:
 *   - WsprNet: the WSPR spots to wsprnet.org as WSJT-X sends them (Network/wsprnet.cpp: HTTP POST
 *     to http://wsprnet.org/post/, function=wspr for each decode; a period with none:
 *     function=wsprstat, the station alive);
 *   - FtLog: the log of the QSOs, an ADIF file as JTDX / WSJT-X write it (wsjtx_log.adi in
 *     %USERPROFILE%/MicronSDR), a record at the end of each QSO (no dialog: the power from the
 *     power meter of the program);
 *   - QrzLog: the QSOs of that file not sent to the QRZ.com logbook yet sent there (one click):
 *     the Logbook API of QRZ.com (https://logbook.qrz.com/api, KEY + ACTION=INSERT + ADIF; the
 *     key: QRZ.com -> My Logbook -> Settings -> API key). The records sent are kept in
 *     qrz_uploaded.txt (a duplicate the logbook has already counts as sent). HTTPS on Windows:
 *     WinHTTP (the TLS of Windows: no OpenSSL DLLs needed).
 */
#ifndef FTNET_H
#define FTNET_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QSet>
#include <QUrlQuery>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

class WsprNet : public QObject
{
    Q_OBJECT
public:
    explicit WsprNet(QObject *parent = 0);
    struct Spot { QString call, grid; int snr, dbm, drift; double dt, freqMHz; };
    // the decodes of one 2 min period (empty: wsprstat); dialHz: the dial frequency (USB);
    // txPct, txHz, dbm: my transmitter (the station information of wsprstat)
    void post(const QString &myCall, const QString &myGrid, qint64 dialHz, qint64 periodStartMs,
              const QList<Spot> &spots, int txPct, qint64 txHz, int dbm);
    int sentTotal() const { return sent; }

signals:
    void status(const QString &text);

private:
    QNetworkAccessManager *nam;
    QList<QUrlQuery> queue;
    bool busy = false;
    int sent = 0;
    void next();
};

namespace FtLog
{
    struct Qso
    {
        QString call, grid, rstSent, rstRcvd, mode, submode;
        QString myCall, myGrid;
        qint64 onMs = 0, offMs = 0;
        qint64 freqHz = 0;              // the TX frequency (dial + audio)
        double powerW = 0;
    };
    QString path();                     // %USERPROFILE%/MicronSDR/wsjtx_log.adi
    QString band(qint64 hz);            // "20m", "" when none
    QString record(const Qso &q);       // one ADIF record (<eor> at its end)
    bool append(const Qso &q, QString *why = 0);
    QStringList records();              // all the records of the file
    QString field(const QString &rec, const QString &name);
    QString key(const QString &rec);    // call|date|time|band: a QSO
}

class QrzLog : public QObject
{
    Q_OBJECT
public:
    explicit QrzLog(QObject *parent = 0);
    int pending() const;                // the QSOs of the log not sent yet
    void upload(const QString &apiKey); // all of them, one after the other
    bool busy() const { return !queue.isEmpty() || inFlight; }

signals:
    void status(const QString &text);
    void finished(int ok, int failed);

private:
    QNetworkAccessManager *nam;
    QStringList queue;
    QString key;
    bool inFlight = false;
    int ok = 0, failed = 0;
    QString lastErr;
    QSet<QString> done() const;
    void markDone(const QString &k);
    void next();
    void post(const QByteArray &body, std::function<void(bool, QByteArray, QString)> done);
};

#endif // FTNET_H
