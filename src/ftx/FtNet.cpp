// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
// wsprnet.org, the ADIF log of the QSOs, the QRZ.com logbook: see FtNet.h
#include "FtNet.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSslSocket>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include "core/ForkName.h"
#include <QRegularExpression>
#include <QDebug>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>
#include <tuple>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX   // AetherSDR: std::min / std::max under MSVC
#endif
#include <windows.h>
#include <winhttp.h>
#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")   // AetherSDR: MicronSDR links it in its .pro
#endif
#endif

// a form body: each value percent encoded ('+' too: a server takes it for a space)
static QByteArray formBody(const QList<QPair<QString, QString>> &items)
{
    QByteArray b;
    for(const auto &it : items)
    {
        if(!b.isEmpty())
            b += '&';
        b += QUrl::toPercentEncoding(it.first) + '=' + QUrl::toPercentEncoding(it.second);
    }
    return b;
}

// ---- wsprnet.org --------------------------------------------------------------------------------

WsprNet::WsprNet(QObject *parent) : QObject(parent), nam(new QNetworkAccessManager(this))
{
}

void WsprNet::post(const QString &myCall, const QString &myGrid, qint64 dialHz, qint64 periodStartMs,
                   const QList<Spot> &spots, int txPct, qint64 txHz, int dbm)
{
    if(myCall.isEmpty() || myGrid.size() < 4 || dialHz <= 0)
    {
        emit status("wsprnet.org: my call and locator are needed");
        return;
    }
    // as WSJT-X: rqrg = the dial + 1500 Hz (the middle of the WSPR band), MHz
    const QString rqrg = QString::number((dialHz + 1500) / 1e6, 'f', 6);
    const QString vers = AetherSDR::forkDisplayName();   // AetherSDR, build 402
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(periodStartMs, Qt::UTC);
    if(spots.isEmpty())
    {
        QUrlQuery q;
        q.addQueryItem("function", "wsprstat");
        q.addQueryItem("rcall", myCall);
        q.addQueryItem("rgrid", myGrid);
        q.addQueryItem("rqrg", rqrg);
        q.addQueryItem("tpct", QString::number(txPct));
        q.addQueryItem("tqrg", QString::number(txHz / 1e6, 'f', 6));
        q.addQueryItem("dbm", QString::number(dbm));
        q.addQueryItem("version", vers);
        q.addQueryItem("mode", "2");
        queue.append(q);
    }
    for(const Spot &s : spots)
    {
        QUrlQuery q;
        q.addQueryItem("function", "wspr");
        q.addQueryItem("date", t.toString("yyMMdd"));
        q.addQueryItem("time", t.toString("HHmm"));
        q.addQueryItem("sig", QString::number(s.snr));
        q.addQueryItem("dt", QString::number(s.dt, 'f', 1));
        q.addQueryItem("drift", QString::number(s.drift));
        q.addQueryItem("tqrg", QString::number(s.freqMHz, 'f', 6));
        q.addQueryItem("tcall", s.call);
        q.addQueryItem("tgrid", s.grid);
        q.addQueryItem("dbm", QString::number(s.dbm));
        q.addQueryItem("version", vers);
        q.addQueryItem("rcall", myCall);
        q.addQueryItem("rgrid", myGrid);
        q.addQueryItem("rqrg", rqrg);
        q.addQueryItem("mode", "2");
        queue.append(q);
    }
    if(queue.size() > 300)
        queue = queue.mid(queue.size() - 300);
    next();
}

void WsprNet::next()
{
    if(busy || queue.isEmpty())
        return;
    busy = true;
    const QUrlQuery q = queue.takeFirst();
    const bool spot = q.queryItemValue("function") == "wspr";
    QNetworkRequest req(QUrl("http://wsprnet.org/post/"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QList<QPair<QString, QString>> items;
    for(const auto &it : q.queryItems(QUrl::FullyDecoded))
        items.append(it);
    QNetworkReply *r = nam->post(req, formBody(items));
    connect(r, &QNetworkReply::finished, this, [this, r, spot]() {
        r->deleteLater();
        busy = false;
        if(r->error() != QNetworkReply::NoError)
            emit status("wsprnet.org: " + r->errorString());
        else
        {
            const QString ans = QString::fromUtf8(r->readAll()).simplified();
            if(spot && ans.contains("spot(s) added"))
                emit status(QString("wsprnet.org: %1 spots sent").arg(++sent));
            else if(spot)
            {
                emit status("wsprnet.org: " + ans.left(80));
                qWarning().noquote() << "WSPRnet:" << ans.left(200);
            }
            else
                emit status(QString("wsprnet.org: %1 spots sent (no decodes now)").arg(sent));
        }
        next();
    });
}

// ---- the ADIF log ------------------------------------------------------------------------------

QString FtLog::path()
{
    return QDir::homePath() + "/AetherSDR-FTX/wsjtx_log.adi";
}

QString FtLog::band(qint64 hz)
{
    static const struct { double lo, hi; const char *b; } B[] = {
        { 1.8, 2.0, "160m" }, { 3.5, 4.0, "80m" }, { 5.06, 5.45, "60m" }, { 7.0, 7.3, "40m" },
        { 10.1, 10.15, "30m" }, { 14.0, 14.35, "20m" }, { 18.068, 18.168, "17m" }, { 21.0, 21.45, "15m" },
        { 24.89, 24.99, "12m" }, { 28.0, 29.7, "10m" }, { 50.0, 54.0, "6m" }, { 144.0, 148.0, "2m" },
        { 70.0, 71.0, "4m" }, { 222.0, 225.0, "1.25m" }, { 420.0, 450.0, "70cm" }, { 1240.0, 1300.0, "23cm" } };   // AetherSDR, 390
    const double mhz = hz / 1e6;
    for(const auto &b : B)
        if(mhz >= b.lo && mhz <= b.hi)
            return b.b;
    return QString();
}

static QString adif(const QString &name, const QString &v)
{
    if(v.isEmpty())
        return QString();
    return QString("<%1:%2>%3 ").arg(name).arg(v.toUtf8().size()).arg(v);
}

QString FtLog::record(const Qso &q)
{
    const QDateTime on = QDateTime::fromMSecsSinceEpoch(q.onMs, Qt::UTC);
    const QDateTime off = QDateTime::fromMSecsSinceEpoch(q.offMs, Qt::UTC);
    QString r;
    r += adif("call", q.call);
    r += adif("gridsquare", q.grid);
    r += adif("mode", q.mode);
    r += adif("submode", q.submode);
    r += adif("rst_sent", q.rstSent);
    r += adif("rst_rcvd", q.rstRcvd);
    r += adif("qso_date", on.toString("yyyyMMdd"));
    r += adif("time_on", on.toString("HHmmss"));
    r += adif("qso_date_off", off.toString("yyyyMMdd"));
    r += adif("time_off", off.toString("HHmmss"));
    r += adif("band", band(q.freqHz));
    r += adif("freq", QString::number(q.freqHz / 1e6, 'f', 6));
    r += adif("station_callsign", q.myCall);
    r += adif("my_gridsquare", q.myGrid);
    if(q.powerW > 0)
        r += adif("tx_pwr", QString::number(q.powerW, 'f', q.powerW < 10 ? 1 : 0));
    r += "<eor>";
    return r;
}

bool FtLog::append(const Qso &q, QString *why)
{
    QDir().mkpath(QDir::homePath() + "/AetherSDR-FTX");
    QFile f(path());
    const bool fresh = !f.exists() || f.size() == 0;
    if(!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        if(why)
            *why = "can't write " + path();
        return false;
    }
    QTextStream o(&f);
    if(fresh)
    {
        // the header as WSJT-X writes it
        o << "WSJT-X ADIF Export<eoh>\n";
    }
    o << record(q) << "\n";
    return true;
}

QStringList FtLog::records()
{
    QFile f(path());
    if(!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringList();
    QString all = QString::fromUtf8(f.readAll());
    const int h = all.indexOf("<eoh>", 0, Qt::CaseInsensitive);
    if(h >= 0)
        all = all.mid(h + 5);
    QStringList out;
    static const QRegularExpression eor("<eor>", QRegularExpression::CaseInsensitiveOption);
    for(QString r : all.split(eor))
    {
        r = r.trimmed();
        if(!r.isEmpty())
            out << r + " <eor>";
    }
    return out;
}

QString FtLog::field(const QString &rec, const QString &name)
{
    const QRegularExpression re("<" + QRegularExpression::escape(name) + ":(\\d+)(:[^>]*)?>",
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(rec);
    if(!m.hasMatch())
        return QString();
    return rec.mid(m.capturedEnd(), m.captured(1).toInt());
}

QString FtLog::key(const QString &rec)
{
    return (field(rec, "call") + "|" + field(rec, "qso_date") + "|" + field(rec, "time_on").left(4) + "|"
            + field(rec, "band")).toUpper();
}

// ---- the QRZ.com logbook -------------------------------------------------------------------------

static QString qrzDonePath()
{
    return QDir::homePath() + "/AetherSDR-FTX/qrz_uploaded.txt";
}

QrzLog::QrzLog(QObject *parent) : QObject(parent), nam(new QNetworkAccessManager(this))
{
}

QSet<QString> QrzLog::done() const
{
    QSet<QString> s;
    QFile f(qrzDonePath());
    if(f.open(QIODevice::ReadOnly | QIODevice::Text))
        while(!f.atEnd())
        {
            const QString l = QString::fromUtf8(f.readLine()).trimmed();
            if(!l.isEmpty())
                s.insert(l);
        }
    return s;
}

void QrzLog::markDone(const QString &k)
{
    QFile f(qrzDonePath());
    if(f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        f.write((k + "\n").toUtf8());
}

int QrzLog::pending() const
{
    const QSet<QString> d = done();
    int n = 0;
    for(const QString &r : FtLog::records())
        if(!d.contains(FtLog::key(r)))
            n++;
    return n;
}

void QrzLog::upload(const QString &apiKey)
{
    if(busy())
        return;
    key = apiKey.trimmed();
    if(key.isEmpty())
    {
        emit status("QRZ.com: no API key (Settings; QRZ.com -> My Logbook -> Settings)");
        emit finished(0, 0);
        return;
    }
#ifndef Q_OS_WIN
    if(!QSslSocket::supportsSsl())
    {
        emit status("QRZ.com: no SSL (OpenSSL)");
        emit finished(0, 0);
        return;
    }
#endif
    const QSet<QString> d = done();
    queue.clear();
    for(const QString &r : FtLog::records())
        if(!d.contains(FtLog::key(r)))
            queue << r;
    ok = failed = 0;
    lastErr.clear();
    if(queue.isEmpty())
    {
        emit status("QRZ.com: nothing new to send");
        emit finished(0, 0);
        return;
    }
    next();
}

// HTTPS POST: on Windows with WinHTTP (the TLS of Windows itself: no OpenSSL DLLs needed), in
// a worker thread; elsewhere with QNetworkAccessManager. done(ok, the answer or the error)
#ifdef Q_OS_WIN
static bool winHttpsPost(const QString &host, const QString &path, const QByteArray &body, QByteArray &ans, QString &err)
{
    bool ok = false;
    HINTERNET ses = WinHttpOpen(L"AetherUglyFork/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = ses ? WinHttpConnect(ses, (LPCWSTR)host.utf16(), INTERNET_DEFAULT_HTTPS_PORT, 0) : 0;
    HINTERNET req = con ? WinHttpOpenRequest(con, L"POST", (LPCWSTR)path.utf16(), 0, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : 0;
    if(req)
    {
        WinHttpSetTimeouts(req, 15000, 15000, 20000, 30000);
        const wchar_t *hdr = L"Content-Type: application/x-www-form-urlencoded\r\n";
        if(WinHttpSendRequest(req, hdr, (DWORD)-1L, (LPVOID)body.constData(), (DWORD)body.size(), (DWORD)body.size(), 0)
           && WinHttpReceiveResponse(req, 0))
        {
            DWORD avail = 0;
            while(WinHttpQueryDataAvailable(req, &avail) && avail > 0)
            {
                QByteArray b((int)avail, 0);
                DWORD got = 0;
                if(!WinHttpReadData(req, b.data(), avail, &got))
                    break;
                ans += b.left((int)got);
            }
            ok = true;
        }
    }
    if(!ok)
        err = QString("no connection to %1 (WinHTTP error %2)").arg(host).arg(GetLastError());
    if(req)
        WinHttpCloseHandle(req);
    if(con)
        WinHttpCloseHandle(con);
    if(ses)
        WinHttpCloseHandle(ses);
    return ok;
}
#endif

void QrzLog::post(const QByteArray &body, std::function<void(bool, QByteArray, QString)> done)
{
#ifdef Q_OS_WIN
    QFutureWatcher<void> *w = new QFutureWatcher<void>(this);
    auto res = std::make_shared<std::tuple<bool, QByteArray, QString>>();
    connect(w, &QFutureWatcher<void>::finished, this, [w, res, done]() {
        w->deleteLater();
        done(std::get<0>(*res), std::get<1>(*res), std::get<2>(*res));
    });
    w->setFuture(QtConcurrent::run([body, res]() {
        QByteArray a;
        QString e;
        const bool ok = winHttpsPost("logbook.qrz.com", "/api", body, a, e);
        *res = std::make_tuple(ok, a, e);
    }));
#else
    QNetworkRequest req(QUrl("https://logbook.qrz.com/api"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setHeader(QNetworkRequest::UserAgentHeader, "AetherUglyFork/1.0");
    QNetworkReply *r = nam->post(req, body);
    connect(r, &QNetworkReply::finished, this, [r, done]() {
        r->deleteLater();
        if(r->error() != QNetworkReply::NoError)
            done(false, QByteArray(), r->errorString());
        else
            done(true, r->readAll(), QString());
    });
#endif
}

void QrzLog::next()
{
    if(queue.isEmpty())
    {
        inFlight = false;
        emit status(QString("QRZ.com: %1 QSOs sent%2%3").arg(ok).arg(failed ? QString(", %1 failed").arg(failed) : QString())
                    .arg(lastErr.isEmpty() ? QString() : " - " + lastErr));
        emit finished(ok, failed);
        return;
    }
    inFlight = true;
    const QString rec = queue.takeFirst();
    const QByteArray body = formBody({ { "KEY", key }, { "ACTION", "INSERT" }, { "ADIF", rec } });
    emit status(QString("QRZ.com: sending %1 ...").arg(FtLog::field(rec, "call")));
    post(body, [this, rec](bool okNet, QByteArray reply, QString err) {
        if(!okNet)
        {
            failed += 1 + queue.size();
            queue.clear();
            lastErr = err;
            qWarning().noquote() << "QRZ:" << err;
            next();
            return;
        }
        // RESULT=OK&LOGID=..., RESULT=FAIL&REASON=..., RESULT=AUTH&...
        const QString ans = QString::fromUtf8(reply).trimmed();
        const QUrlQuery a(ans);
        const QString res = a.queryItemValue("RESULT", QUrl::FullyDecoded).toUpper();
        const QString reason = a.queryItemValue("REASON", QUrl::FullyDecoded);
        if(res == "OK" || res == "REPLACE" || reason.contains("duplicate", Qt::CaseInsensitive))
        {
            ok++;
            markDone(FtLog::key(rec));
        }
        else if(res == "AUTH")
        {
            failed += 1 + queue.size();
            queue.clear();
            lastErr = "the API key is not accepted (or the logbook not allowed for it)";
        }
        else
        {
            failed++;
            qWarning().noquote() << "QRZ:" << FtLog::field(rec, "call") << ans.left(200);
            lastErr = FtLog::field(rec, "call") + ": " + (reason.isEmpty() ? ans.left(60) : reason.left(60));
        }
        next();
    });
}
