// ANAN fork build 406: this file is MicronSDR's (N7DDC), taken as it is for the
// JS8 sub-mode of FTX in this program; the changes for it are marked "AetherSDR".
/*
 * Js8Panel - the window "MicronSDR JS8 Decoder": the JS8 receiver (JS8Call: decoding only), a
 * window of its own while the JS8 mode is on (button JS8 of the bottom bar; closing the window
 * ends JS8); its place and size are kept. As the FT window, with its own settings (Options.ini,
 * group JS8):
 *   - the submode (Normal / Fast / Turbo / Slow), the band and the dial frequency;
 *   - UTC, the T/R period, DT, the input level;
 *   - Band activity: one line per audio offset, the text of the last frames there;
 *   - Messages: the messages put together from their frames (the first .. the last frame of a
 *     message, by the offset), the end marked with the diamond as in JS8Call;
 *   - Stations: the callsigns heard (heartbeats, CQs, directed messages), locator, country,
 *     distance; PSK Reporter spots (mode JS8).
 * The frames are decoded by libjs8.dll (JS8_src), unpacked by the varicode of JS8Call (js8/).
 *
 * The transmitter (as JS8Call, Js8PanelTx.cpp): the outgoing text -> the frames of the varicode of
 * JS8Call (buildMessageFrames: heartbeat / CQ, directed, data frames) -> the tones (js8_encode of
 * libjs8.dll) -> FtTx::js8Wave, one frame each period, back to back, from the start delay of the
 * period (late by up to the dead air of the period: from that point, as JS8Call):
 *   - the selected callsign (Stations: a click; "To") gets the text directed to it;
 *   - CQ, HB (repeated every N min when set), Reply, SNR, the Directed menu (queries / answers);
 *   - AUTO: the automatic replies to the queries to my call (SNR?, GRID?, INFO?, STATUS?,
 *     HEARING?, AGN?, QUERY MSGS, MSG -> ACK), the heartbeat acknowledgements (HB on); with
 *     the confirmation (90 s) unless switched off; not while idle (the TX watchdog);
 *   - one offset for RX and TX (as JS8Call); heartbeats and their answers: a free offset of
 *     500 .. 1000 Hz, then back.
 */
#ifndef JS8PANEL_H
#define JS8PANEL_H

#include <QWidget>
#include <QHash>
#include <QMap>
#include <QDateTime>
#include <QPair>
#include <QSet>
#include <QQueue>
#include "FtUnit.h"
#include "FtTx.h"
#include "FtWaterfall.h"   // AetherSDR, build 409
#include <QPointer>

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
class QPlainTextEdit;
class QSettings;
class QVBoxLayout;
class QGridLayout;
class DecodedText;
class PskReporter;
class QToolButton;
class QMenu;

class Js8Panel : public QWidget
{
    Q_OBJECT
    bool pinned = false;                // build 454: attached (its place is the main window's)
    QPushButton *pbPin = nullptr;
    QByteArray winGeom;                 // the place of the window (Options.ini JS8/WindowGeometry)
public:
    Js8Panel(FtUnit *unit, QWidget *parent = 0);
    ~Js8Panel();

    void loadSettings();
    void saveSettings();
    int  mode() const { return curMode; }   // FtUnit::JS8N / JS8F / JS8T / JS8S
    void setMode(int mode);
    qint64 dialHz() const { return dial; }
    void setDialShown(qint64 hz);
    void start();
    void stop();
    void showWindow(const QRect &mainGeom);     // shown (its place kept; the first time: next to the main window)
    void hideWindow();
    // AetherSDR, build 454: attached to the right of the main window (the main window places it)
    void setPinned(bool on);
    bool isPinned() const { return pinned; }

    // the transmitter: the main window plays its audio (audio thread) and keys MOX on pttRequest
    FtTx *txUnit() { return &jtx; }
    int  txFreqHz() const { return rxFreq; }
    int  txBandwidthHz() const { return 8 * 12000 / symbolSamples(curMode); }
    void txAborted();                           // MOX released by hand (or refused): the message stopped
    void txNote(const QString &text);           // a word of the main window on the TX (shown)
    void setTxAllowed(bool on);                 // SDR-Micron: no transmitter
    // AetherSDR, build 409 (operator, 14.11): a waterfall of its own, as the
    // FTX window's -- the main window puts it under its panorama while JS8 is on
    FtWaterfall *waterfall() const { return wf; }

signals:
    void tuneRequest(qint64 dialHz);
    void modeChanged(int mode);
    void pinToggled(bool on);           // AetherSDR, build 454: the pin -- attached to the main window or not
    void closed();                      // the window closed by the user: JS8 off
    void pttRequest(bool on);           // the transmitter: MOX on / off
    void ftxRequested();                // AetherSDR, build 406: the FTX button -- the FT8 / FT4 / WSPR window

protected:
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;     // Enter in the outgoing text: Send

private slots:
    void onDecoded(qint64 periodStartMs, int mode, QVector<FtUnit::Decode> list, int decodeMs, bool early = false);
    void onTick();
    void onBand(int index);
    void onFreqEdited();
    void applySettings();

private:
    struct Line                         // a message (its frames so far)
    {
        QDateTime t;
        int off, snr;
        QString text;
        bool done;
        bool hb;                        // a heartbeat / CQ or its automatic reply (HEARTBEAT SNR)
        bool tx = false;                // my transmission
        bool toMe = false;              // directed to my call
    };
    struct Act                          // band activity: one offset
    {
        QDateTime last;
        int snr;
        QString text;
    };
    struct Station
    {
        QString call, grid, country, cont;
        int bestSnr, lastSnr, count, off;
        double km, az;
        QDateTime last;
    };
    FtUnit *unit;
    QPointer<FtWaterfall> wf;           // AetherSDR, build 409
    void wfSettings();
    PskReporter *psk;
    int curMode;
    qint64 dial;
    bool running;
    int rxFreq;
    int autoSaved;

    QButtonGroup *bgMode;
    QComboBox *cbBand, *cbFreq;
    QLabel *lbUtc, *lbDt, *lbLevel, *lbStat, *lbRx, *lbPsk;
    QProgressBar *pbPeriod;
    QTableWidget *twBand, *twSta;
    QPlainTextEdit *pteMsg;
    QPushButton *pbSettings, *pbClear;
    QGroupBox *gbSet;
    QLineEdit *leCall, *leGrid, *leAnt, *leRig;
    QCheckBox *chbPsk, *chbAutoT, *chbHb, *chbEarly, *chbMiles;
    // the frames of the period shown (early + full decode: once): frame, offset
    QList<QPair<QString, int> > seenFrames;
    qint64 seenPeriod = 0;
    int earlyCount = 0;
    QSpinBox *sbFa, *sbFb, *sbOffset;
    QTimer *tick;

    QList<Line> lines;
    QMap<int, Act> acts;                // offset (Hz) -> activity
    QHash<QString, Station> stations;
    QList<double> dtHist;

    static const int BANDS = 10;
    static const char *bandName[BANDS];
    static const qint64 defFreq[BANDS];
    qint64 freqs[BANDS];
    int band;
    void fillBands();
    void tuneBand();
    int  actKey(int off) const;         // an activity within +-10 Hz, or off
    void addStation(const QString &call, const QString &grid, int snr, int off, const QDateTime &t);
    void refreshBand();
    void refreshMessages();
    void refreshStations();
    static int submodeOf(int mode) { return mode - FtUnit::JS8N; }

    // ---- the transmitter (Js8PanelTx.cpp) ----
    enum { PRIO_LOW = 10, PRIO_NORMAL = 50, PRIO_HIGH = 100 };
    struct TxFrame { QString frame; int bits; };
    struct Outgoing                     // a message waiting for its turn (replies, heartbeats)
    {
        int prio;
        QString text;
        int off;                        // its offset, -1: the one now
        qint64 tMs;
        QString why;
    };
    struct Cmd                          // a directed command heard (JS8Call: CommandDetail)
    {
        QString from, to, cmd, extra, text, grid;
        int snr = 0, off = 0, bits = 0;
        qint64 tMs = 0;
    };
    FtTx jtx;
    bool txAllowed = true;
    QLineEdit *leTo = nullptr;
    QPushButton *pbDesel = nullptr, *pbCq = nullptr, *pbHb = nullptr, *pbReply = nullptr, *pbSnr = nullptr,
                *pbSend = nullptr, *pbHalt = nullptr, *pbAuto = nullptr;
    QToolButton *tbDir = nullptr;
    QMenu *menuDir = nullptr;
    QPlainTextEdit *pteTx = nullptr;
    QSpinBox *sbOff = nullptr;
    QLabel *lbTxState = nullptr, *lbNext = nullptr;
    QWidget *wConfirm = nullptr;
    QLabel *lbConfirm = nullptr;
    QPushButton *pbConfYes = nullptr, *pbConfNo = nullptr;
    QLineEdit *leInfo = nullptr, *leStatus = nullptr, *leCqMsg = nullptr, *leReplyMsg = nullptr;
    QSpinBox *sbHbMin = nullptr, *sbCqMin = nullptr, *sbWatchdog = nullptr;
    QCheckBox *chbHbAck = nullptr, *chbConfirm = nullptr, *chbHoldPtt = nullptr, *chbHbPause = nullptr;
    QTimer *txTick = nullptr;
    QList<Outgoing> confQ;              // the automatic replies waiting for the confirmation

    QQueue<TxFrame> txQueue;            // the frames of the message being sent, not sent yet
    int txFramesSent = 0, txFramesTotal = 0;
    QString txAllText;                  // the whole message as it will be decoded
    QString txSentText;                 // the frames sent so far, as decoded
    bool sending = false;               // a message in progress (its frames, the gaps between them)
    bool txOn = false;                  // a frame on the air now
    bool pttHeld = false;               // MOX kept between the frames (Hold PTT)
    qint64 txIdx = -1, lastTxIdx = -1;
    qint64 lastTxStartMs = 0;
    QString lastTxMessage;              // AGN?
    int restoreOff = -1;                // the offset before a heartbeat elsewhere
    QSet<qint64> txPeriodsMs;           // the periods of my transmissions: not decoded
    QList<Outgoing> outQ;
    qint64 confirmUntil = 0;
    qint64 nextHbMs = 0, nextCqMs = 0;
    qint64 lastAction = 0;
    bool idle = false;                  // the TX watchdog fired: no automatic transmissions
    QHash<QString, qint64> hbAcked;     // the heartbeats answered (once in 15 min a station)
    QMap<int, Cmd> bufs;                // the buffered commands (MSG, ...) by offset, their text so far
    qint64 lastSecond = 0;

    void buildTxUi(QVBoxLayout *v);
    void addTxSettings(QGridLayout *g, int row);
    void loadTxSettings(QSettings &st);
    void saveTxSettings(QSettings &st);
    void txUi();
    void userAction();
    QString myCall() const;
    QString myGrid() const;             // as typed (4 or 6)
    QString selectedCall() const;
    void selectCall(const QString &call);
    QString macros(const QString &text) const;
    static QString cleanText(const QString &text);
    bool sendText(const QString &text, bool fromUser, bool useSelected = true);    // a message sent now
    void onTxTick();
    void startFrame(qint64 idx);
    void finishFrame();
    void endMessage(bool completed);
    void haltTx();
    void markSent();
    void addTxLine(const QString &text, qint64 periodStartMs);
    void enqueue(int prio, const QString &text, int off, const QString &why);
    void processOutQ();
    void askConfirm(const Outgoing &o);
    void showConfirm();
    void sendCq();
    void sendHb();
    void directed(const QString &cmd, bool now);
    bool isOffFree(int f, int bw) const;
    int  freeOffset(int fmin, int fmax, int bw) const;
    void heard(const FtUnit::Decode &d, const DecodedText &dt, qint64 tMs);
    void command(const Cmd &c);
    void storeMsg(const Cmd &c);
    static int symbolSamples(int mode);
    static int startDelayMs(int mode);
    int  lateMs() const;
    static QString baseCall(const QString &call);
};

#endif // JS8PANEL_H
