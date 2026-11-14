// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
/*
 * FtPanel - the window "MicronSDR FTX Decoder": the FT8 / FT4 / WSPR receiver (decoding only),
 * a window of its own while the FTX mode is on (button FTX of the bottom bar; closing the
 * window ends FTX); its place and size are kept:
 *   - the mode (FT8 / FT4 / WSPR), the band and the dial frequency (the standard frequencies,
 *     editable; WSPR: the WSPR dial frequencies, the signals at 1400 .. 1600 Hz);
 *   - UTC, the T/R period, DT of the stations heard, the input level;
 *   - Band activity: all the decodes (UTC, dB, DT, Hz, message, country, km, azimuth);
 *   - Stations: everybody heard (callsign, locator, country, distance, best / last SNR, ...);
 *   - the settings of the mode (callsign, locator, depth, AP, range, time correction, PSK
 *     Reporter with the antenna, FT8: the early decode at 11.8 s, the distances in km or
 *     miles); all saved in Options.ini (group FT).
 *   WSPR: 2 min periods (even minutes), the messages "CALL GRID dBm"; the column AP shows the
 *   drift (Hz); AP and the search range are not used; no PSK Reporter spots (WSPR spots
 *   belong to wsprnet.org).
 * Country, continent, zones: cty.dat (AD1C, next to the program).
 */
#ifndef FTPANEL_H
#define FTPANEL_H

#include <QWidget>
#include <QHash>
#include <QSet>
#include <QDateTime>
#include <QPointer>
#include "FtUnit.h"
#include "FtTx.h"
#include "FtNet.h"

class QTableWidget;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QGroupBox;
class QTimer;
class QButtonGroup;
class QGridLayout;
class QVBoxLayout;
class QSettings;
class PskReporter;
#include "FtWaterfall.h"

class FtPanel : public QWidget
{
    Q_OBJECT
public:
    FtPanel(FtUnit *unit, QWidget *parent = 0);
    ~FtPanel();

    void loadSettings();
    void saveSettings();
    int  mode() const { return curMode; }
    void setMode(int mode);             // FtUnit::FT8 / FT4 / WSPR
    qint64 dialHz() const { return dial; }
    void setDialShown(qint64 hz);       // the dial frequency the receiver is on
    void start();                       // the FT mode on: tune, decode
    void stop();
    void showWindow(const QRect &mainGeom);     // shown (its place kept; the first time: next to the main window)
    void hideWindow();
    FtWaterfall *waterfall() const { return wf; }  // the main window puts it under its panorama
    // the transmitter: the main window plays its audio (audio thread) and keys MOX on pttRequest
    FtTx *txUnit() { return &ftx; }
    bool transmitting() const { return txOn; }
    int  txFreqHz() const { return txFreq; }
    int  rxFreqHz() const { return rxFreq; }
    // the transmission on: its period (UTC ms / the period) and its message (the remote mode: the
    // server makes the waveform and keys its transmitter at the time, micron-server "FTX transmit")
    qint64  txPeriodIdx() const { return txIdx; }
    QString txMessageNow() const { return txMsgNow; }
    void txAborted();                           // MOX released by hand (or refused): TX off, Enable TX off
    void setTxAllowed(bool on);                 // SDR-Micron (a receiver): Enable TX off and disabled
    void txNote(const QString &text);           // a word of the main window on the TX (shown)
    void notePower(double watts);               // the power meter while FTX transmits (the log)
    int waterfallHeight() const { return wfHeight; }
    static const int PANEL_WIDTH = 600;         // (not used: both decoder windows are windows of their own)

signals:
    void tuneRequest(qint64 dialHz);    // the receiver to this dial frequency (USB)
    void modeChanged(int mode);
    void closed();                      // the window closed by the user: FTX off
    void pttRequest(bool on);           // the transmitter: MOX on / off
    void txRearmed();                   // the message changed just after the start (the same period)
    void txHalted();                    // Halt TX (the remote server: its transmission off now)

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private slots:
    void onDecoded(qint64 periodStartMs, int mode, QVector<FtUnit::Decode> list, int decodeMs, bool early = false);
    void onTick();
    void onBand(int index);
    void onFreqEdited();
    void applySettings();

private:
    struct Entity { QString name, cont; int cq, itu; double lat, lon; };
    struct Station
    {
        QString call, grid, country, cont;
        int bestSnr, lastSnr, count;
        double km, az;
        QDateTime last;
        bool cq;
    };
    FtUnit *unit;
    PskReporter *psk;
    int curMode;
    qint64 dial;
    bool running;
    int rxFreq, txFreq;
    int autoSaved;                     // the automatic time correction saved (ms)

    QButtonGroup *bgMode;
    QComboBox *cbBand, *cbFreq;
    QLabel *lbUtc, *lbDt, *lbLevel, *lbStat, *lbRx, *lbPsk;
    QProgressBar *pbPeriod;
    QTableWidget *twAct;
    bool colFitting = false;                   // the columns set by the program (not by hand)
    void fitColumns();
    void onColumnResized(int col, int oldSize, int newSize);
    QPushButton *pbSettings, *pbClear;
    QCheckBox *pbWf;                           // the waterfall shown (settings)
    QLabel *lbTx;
    QPointer<FtWaterfall> wf;
    int wfHeight = 170;
    QSpinBox *sbWf0, *sbWf1;
    void showWaterfall();
    void wfSettings();                         // the range, the search range, RX / TX, the mode
    QGroupBox *gbSet;
    QLineEdit *leCall, *leGrid, *leAnt, *leRig;
    QComboBox *cbDepth;
    QCheckBox *chbAp, *chbPsk, *chbAutoT, *chbEarly, *chbMiles;
    QSet<QString> shownMsgs;           // the messages of the period shown (early + full: once)
    int earlyCount = 0;                // the early decodes of msgsPeriod
    qint64 msgsPeriod = 0;             // the period of shownMsgs
    QString dist(double km) const;     // km or miles (the setting), "" for none
    void distUnitsUi();
    QSpinBox *sbFa, *sbFb, *sbOffset;
    QTimer *tick;

    // cty.dat
    QVector<Entity> ents;
    QHash<QString, int> pfx;           // prefix -> entity
    QHash<QString, int> exact;         // =CALL -> entity
    bool ctyLoaded;
    // AetherSDR, build 389: what the log holds, for the highlighting of the decodes (the rules of
    // JTDX, displaytext.cpp): the calls and the entities worked, at all and on a band ("CALL|20m").
    QSet<QString> wkCall, wkCallBand, wkEntBand;
    QSet<int> wkEnt;
    bool wkLoaded = false;
    void loadWorked();
    void addWorked(const QString &call, const QString &band);
    void loadCty();
    int  entityOf(const QString &call) const;

    // everybody heard (not shown): the locators of the stations for the PSK Reporter spots of the
    // messages without one
    QHash<QString, Station> stations;
    QList<double> dtHist;              // DT of the last decodes (average)
    qint64 lastPeriodShown;

    static const int BANDS = 11;
    static const char *bandName[BANDS];
    static const qint64 defFreq[3][BANDS];     // FT8, FT4, WSPR (0 = none)
    qint64 freqs[3][BANDS];                    // the frequencies in use (edited ones saved)
    int    bandOf[3];                          // the band of each mode
    static int modeIndex(int mode) { return mode == FtUnit::FT8 ? 0 : mode == FtUnit::FT4 ? 1 : 2; }
    static QString modeKey(int mi) { return mi == 2 ? QString("W") : QString::number(mi); }   // Options.ini (2 was FT2)
    void modeUi();                             // WSPR: the controls it does not use off
    QByteArray winGeom;                        // the place of the window (Options.ini FT/WindowGeometry)
    void fillBands();
    void tuneBand();
    void addStation(const QString &call, const QString &grid, int snr, bool cq, const QDateTime &t);
    // ---- the transmitter (FtPanelTx.cpp) ----
    FtTx ftx;
    QWidget *wTx;
    QLineEdit *leDx, *leDxGrid;
    QSpinBox *sbRpt;
    QPushButton *pbEnable, *pbHalt, *pbGen;
    QCheckBox *chbTx1st, *chbAutoSeq;
    QLabel *lbTxState, *lbDxLbl, *lbGridLbl, *lbRptLbl;
    QLineEdit *leTx[6];
    QPushButton *rbTx[6];
    QButtonGroup *bgTx;
    QCheckBox *chbHoldTx, *chbOff73;
    QSpinBox *sbWatchdog, *sbWsprPwr, *sbWsprPct;
    QTimer *txTick;
    bool txOn = false;
    qint64 txIdx = -1, lastTxIdx = -1;          // the period of the TX now / the last one (index)
    int txSent = -1;                            // the Tx message (0..5) sent last
    QString txMsgNow;                           // the text of the message on the air
    qint64 lastAction = 0;                      // the watchdog: the last action of the user (ms)
    QSet<qint64> txPeriodsMs;                   // the periods of my transmissions: not decoded
    struct Enc { QVector<int> tones; QString sent; };
    QHash<QString, Enc> encCache;
    QSet<QString> encPending;
    qint64 wsprDecidedIdx = -1;
    bool wsprTxThis = false;
    bool rr73Sent = false;                      // RR73 sent: the answer of the DX waited for
    // wsprnet.org, the log of the QSOs, QRZ.com (FtNet.h)
    WsprNet *wnet;
    QrzLog *qrz;
    QCheckBox *chbWsprNet;
    QLineEdit *leQrzKey;
    QPushButton *pbQrz;
    FtLog::Qso qso;                             // the QSO now (its call empty: none)
    bool qsoLogged = false;
    double qsoPowerW = 0;
    void startQso(const QString &call, const QString &grid);
    void logQso();
    void qrzUi();
    void wsprSpots(qint64 periodStartMs, const QVector<FtUnit::Decode> &list);
    void buildTxUi(QVBoxLayout *v);
    void addTxSettings(QGridLayout *g, int row);
    void loadTxSettings(QSettings &st);
    void saveTxSettings(QSettings &st);
    void genMessages();
    void selectTx(int i);
    void onTxTick();
    void startTx(qint64 idx);
    void finishTx();
    void haltTx();
    void userAction();
    void onEncoded(int mode, QString msg, QVector<int> tones, QString sent);
    void autoSeq(qint64 periodStartMs, const QVector<FtUnit::Decode> &list, bool early);
    int  replyFor(const QStringList &w, bool &done) const;
    void callStation(int row);
    void addTxRow(const QString &msg, qint64 periodStartMs);
    QString encKey(int mode, const QString &m) const { return QString::number(mode) + "|" + m; }
    QString currentTxMsg() const;
    bool encodedNow(const QString &msg, Enc &e);    // cached, else asked for (false)
    void txUi();
    std::vector<float> waveOf(const Enc &e) const;
    int  txStartOffMs() const { return curMode == FtUnit::WSPR ? 1000 : 500; }
    int  txLateMs() const { return curMode == FtUnit::WSPR ? 2000 : curMode == FtUnit::FT4 ? 1000 : 2000; }
    static QString rpt(int db);
    static QString strip(const QString &call) { QString c = call; return c.remove('<').remove('>'); }

    static bool isCall(const QString &w);
    static bool isGrid4(const QString &w);
    static bool isGrid6(const QString &w);
    static bool gridToLatLon(const QString &g, double &lat, double &lon);
    static void distAz(double lat1, double lon1, double lat2, double lon2, double &km, double &az);
};

#endif // FTPANEL_H
