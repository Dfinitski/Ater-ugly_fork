// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
/*
 * FtWaterfall - the waterfall of the FT8 / FT4 / WSPR mode (as the Wide Graph of WSJT-X / JTDX):
 * a strip at the bottom of the panorama of the main window (WdgGraph::setBottomWidget), shown
 * while FTX is on (Waterfall in the settings of the FTX window); its height: the left mouse button
 * on the frequency scale dragged up / down:
 *   - the waterfall of the decoder audio (FtUnit: FFT 2048 at 12 kHz = 5.86 Hz, a line 0.34 s),
 *     the newest line at the top, the colours of JTDX (dark blue noise .. cyan .. bright yellow);
 *     the levels set by themselves (the noise floor of each line, the span of the signals);
 *   - a line at the start of each T/R period with its UTC;
 *   - the frequency scale on top (Hz), the search range of the decoder under it;
 *   - the RX band: green, the TX band: red (two lines each: the start and the end of the signal,
 *     FT8 50 Hz, FT4 83 Hz, WSPR 6 Hz; on the scale a bracket). The left mouse button on the
 *     waterfall sets the RX frequency, the right one the TX frequency: the band centred on the click;
 *     the button held: the band follows the mouse.
 * The range shown: the settings of the FTX window, by default 0 .. 4000 Hz (the band pass of
 * the decoder audio: 100 .. 4000 Hz).
 */
#ifndef FTWATERFALL_H
#define FTWATERFALL_H

#include <QWidget>
#include <QImage>
#include <QVector>

class FtUnit;
class QTimer;

class FtWaterfall : public QWidget
{
    Q_OBJECT
public:
    FtWaterfall(FtUnit *unit, QWidget *parent = 0);

    void setRange(int fromHz, int toHz);
    void setRx(int hz) { rx = hz; update(); }
    void setTx(int hz) { tx = hz; update(); }
    void setSearch(int fa, int fb) { sa = fa; sb = fb; update(); }
    void setMode(int mode);             // FtUnit::FT8 / FT4 / WSPR: the band of the signal, the period
    void setRunning(bool on);           // takes the lines of the unit
    // AetherSDR, build 395: held while the transmitter is on (no lines of the own signal)
    void setPaused(bool on);
    // AetherSDR, build 409: the JS8 window's waterfall -- one frequency (it
    // transmits where it receives): one band drawn, both mouse buttons set it
    void setSingle(bool on) { single = on; update(); }
    void clear();

signals:
    void rxClicked(int hz);
    void txClicked(int hz);
    void heightRequested(int h);        // the scale dragged: this height wanted

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    FtUnit *unit;
    QTimer *timer;
    bool runWanted = false, paused = false;   // AetherSDR, 395
    bool single = false;                      // AetherSDR, 409
    void applyRun();
    QImage img;                         // the waterfall (without the scale)
    int f0 = 0, f1 = 4000;
    int rx = 1500, tx = 1500, sa = 200, sb = 3500;
    int mode;
    double bw = 50.0;
    qint64 lastPeriod = -1;
    float floorDb = 0.0f, spanDb = 30.0f;
    bool levelInit = false;
    int hoverX = -1;
    int dragY0 = -1, dragH0 = 0;        // the scale dragged: the mouse (global y) and the height at the start
    Qt::MouseButton freqDrag = Qt::NoButton;    // a button held on the waterfall: its band follows the mouse
    void setFreqAt(int x);
    QRgb pal[256];
    static const int SCALE_PX = 26;

    void onTimer();
    void addLine(const QVector<float> &db, qint64 tMs);
    double xToHz(int x) const;
    int hzToX(double hz) const;
};

#endif // FTWATERFALL_H
