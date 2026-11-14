// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
// PSK Reporter: see PskReporter.h
#include "PskReporter.h"
#include <QUdpSocket>
#include <QDataStream>
#include <QRandomGenerator>
#include <QDebug>

static const char *PSKR_HOST = "report.pskreporter.info";
static const quint16 PSKR_PORT = 4739;
static const int SEND_INTERVAL_MS = 120000;
static const qint64 REPEAT_MS = 5 * 60 * 1000;

namespace {

void writeStr(QDataStream &out, const QString &s)
{
    const QByteArray u = s.toUtf8().left(254);
    out << quint8(u.size());
    out.writeRawData(u.constData(), u.size());
}

// a set: its length into bytes 2..3, padded to 4 bytes
void finishSet(QByteArray &set)
{
    while(set.size() % 4)
        set.append('\0');
    set[2] = (char)((set.size() >> 8) & 0xFF);
    set[3] = (char)(set.size() & 0xFF);
}

} // namespace

PskReporter::PskReporter(const QString &program, QObject *parent)
    : QObject(parent), sock(new QUdpSocket(this)), prog(program), on(false), seq(0),
      obsId(QRandomGenerator::global()->generate()), descriptorsLeft(3), sent(0)
{
    timer.setInterval(SEND_INTERVAL_MS);
    connect(&timer, &QTimer::timeout, this, &PskReporter::flush);
}

PskReporter::~PskReporter()
{
    if(on)
        flush();
}

void PskReporter::setLocalStation(const QString &call, const QString &grid, const QString &antenna, const QString &rig)
{
    if(call.trimmed().toUpper() != myCall || grid.trimmed() != myGrid || antenna.trimmed() != myAnt || rig.trimmed() != myRig)
        descriptorsLeft = 3;
    myCall = call.trimmed().toUpper();
    myGrid = grid.trimmed();
    myAnt = antenna.trimmed();
    myRig = rig.trimmed();
}

void PskReporter::setEnabled(bool en)
{
    if(en == on)
        return;
    if(!en)
        flush();
    on = en;
    if(on)
    {
        descriptorsLeft = 3;
        sock->abort();
        sock->connectToHost(PSKR_HOST, PSKR_PORT, QIODevice::WriteOnly);
        timer.start();
        emit status("PSK Reporter: on");
    }
    else
    {
        timer.stop();
        sock->abort();
        spots.clear();
        emit status("PSK Reporter: off");
    }
}

void PskReporter::addSpot(const QString &call, const QString &grid, qint64 freqHz, const QString &mode, int snr, const QDateTime &timeUtc)
{
    if(!on || call.isEmpty())
        return;
    const QString key = call + "/" + QString::number(freqHz / 1000000);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto it = recent.find(key);
    if(it != recent.end() && now - it.value().ms < REPEAT_MS && (it.value().grid || grid.isEmpty()))
        return;
    recent[key] = Recent { now, !grid.isEmpty() };
    Spot s { call, grid, mode, freqHz, qBound(-128, snr, 127), timeUtc };
    spots.append(s);
    if(recent.size() > 5000)            // old entries out
    {
        for(auto r = recent.begin(); r != recent.end(); )
            r = (now - r.value().ms > REPEAT_MS) ? recent.erase(r) : r + 1;
    }
}

QByteArray PskReporter::buildPacket(const QList<Spot> &list)
{
    QByteArray msg;
    QDataStream m(&msg, QIODevice::WriteOnly);
    m << quint16(10) << quint16(0) << quint32(QDateTime::currentSecsSinceEpoch()) << ++seq << obsId;
    if(descriptorsLeft > 0)
    {
        descriptorsLeft--;
        QByteArray t;
        QDataStream o(&t, QIODevice::WriteOnly);
        o << quint16(2) << quint16(0)                       // template set
          << quint16(0x50e3) << quint16(7)
          << quint16(0x8000 + 1) << quint16(0xffff) << quint32(30351)      // senderCallsign
          << quint16(0x8000 + 5) << quint16(5) << quint32(30351)           // frequency
          << quint16(0x8000 + 6) << quint16(1) << quint32(30351)           // sNR
          << quint16(0x8000 + 10) << quint16(0xffff) << quint32(30351)     // mode
          << quint16(0x8000 + 3) << quint16(0xffff) << quint32(30351)      // senderLocator
          << quint16(0x8000 + 11) << quint16(1) << quint32(30351)          // informationSource
          << quint16(150) << quint16(4);                                   // flowStartSeconds
        finishSet(t);
        msg.append(t);
        QByteArray r;
        QDataStream q(&r, QIODevice::WriteOnly);
        q << quint16(3) << quint16(0)                       // options template set (as WSJT-X 2.7+)
          << quint16(0x50e2) << quint16(5) << quint16(1)    // 5 fields, the receiver call the scope
          << quint16(0x8000 + 2) << quint16(0xffff) << quint32(30351)      // receiverCallsign
          << quint16(0x8000 + 4) << quint16(0xffff) << quint32(30351)      // receiverLocator
          << quint16(0x8000 + 8) << quint16(0xffff) << quint32(30351)      // decodingSoftware
          << quint16(0x8000 + 9) << quint16(0xffff) << quint32(30351)      // antennaInformation
          << quint16(0x8000 + 13) << quint16(0xffff) << quint32(30351);    // rigInformation
        finishSet(r);
        msg.append(r);
    }
    {
        QByteArray r;
        QDataStream q(&r, QIODevice::WriteOnly);
        q << quint16(0x50e2) << quint16(0);
        writeStr(q, myCall);
        writeStr(q, myGrid);
        writeStr(q, prog);
        writeStr(q, myAnt);
        writeStr(q, myRig);
        finishSet(r);
        msg.append(r);
    }
    if(!list.isEmpty())
    {
        QByteArray d;
        QDataStream q(&d, QIODevice::WriteOnly);
        q << quint16(0x50e3) << quint16(0);
        for(const Spot &s : list)
        {
            writeStr(q, s.call);
            q << quint8((s.freq >> 32) & 0xFF) << quint8((s.freq >> 24) & 0xFF) << quint8((s.freq >> 16) & 0xFF)
              << quint8((s.freq >> 8) & 0xFF) << quint8(s.freq & 0xFF) << qint8(s.snr);
            writeStr(q, s.mode);
            writeStr(q, s.grid);
            q << quint8(1) << quint32(s.time.toSecsSinceEpoch());
        }
        finishSet(d);
        msg.append(d);
    }
    msg[2] = (char)((msg.size() >> 8) & 0xFF);
    msg[3] = (char)(msg.size() & 0xFF);
    return msg;
}

void PskReporter::flush()
{
    if(!on || myCall.isEmpty() || myGrid.size() < 4)
        return;
    if(sock->state() != QAbstractSocket::ConnectedState)
    {
        sock->abort();
        sock->connectToHost(PSKR_HOST, PSKR_PORT, QIODevice::WriteOnly);
        emit status("PSK Reporter: connecting");
        return;                         // the spots stay for the next time
    }
    // up to about 50 spots per datagram (well under 1400 bytes)
    while(!spots.isEmpty() || descriptorsLeft > 0)
    {
        QList<Spot> part = spots.mid(0, 50);
        spots = spots.mid(part.size());
        const QByteArray p = buildPacket(part);
        if(sock->write(p) < 0)
        {
            emit status("PSK Reporter: " + sock->errorString());
            return;
        }
        sent += part.size();
        if(spots.isEmpty())
            break;
    }
    emit status(QString("PSK Reporter: %1 spots sent").arg(sent));
}
