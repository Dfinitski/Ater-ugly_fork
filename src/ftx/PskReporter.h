// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
/*
 * PskReporter - spots to PSK Reporter (pskreporter.info): the IPFIX format over UDP to
 * report.pskreporter.info:4739, as WSJT-X does (Network/PSKReporter.cpp: the same templates).
 * The spots are collected and sent every 2 minutes (and at stop()); a station heard again
 * within 5 minutes on the same band is not sent again - unless its locator is known now and was
 * not in the spot sent (a QSO message first, its CQ with the locator later).
 */
#ifndef PSKREPORTER_H
#define PSKREPORTER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QTimer>

class QUdpSocket;

class PskReporter : public QObject
{
    Q_OBJECT
public:
    explicit PskReporter(const QString &program, QObject *parent = 0);
    ~PskReporter();

    void setLocalStation(const QString &call, const QString &grid, const QString &antenna, const QString &rig);
    void setEnabled(bool on);
    bool enabled() const { return on; }
    // a station heard: callsign, its locator ("" = none), the RF frequency (Hz), mode, SNR
    void addSpot(const QString &call, const QString &grid, qint64 freqHz, const QString &mode, int snr, const QDateTime &timeUtc);
    void flush();                   // send what is collected now
    int  sentTotal() const { return sent; }
    int  pending() const { return spots.size(); }

signals:
    void status(const QString &text);

private:
    struct Spot { QString call, grid, mode; qint64 freq; int snr; QDateTime time; };
    QUdpSocket *sock;
    QTimer timer;
    QString prog, myCall, myGrid, myAnt, myRig;
    bool on;
    quint32 seq, obsId;
    int descriptorsLeft;
    QList<Spot> spots;
    struct Recent { qint64 ms; bool grid; };
    QMap<QString, Recent> recent;   // call + band -> the last spot sent (ms, with a locator)
    int sent;
    QByteArray buildPacket(const QList<Spot> &list);
};

#endif // PSKREPORTER_H
