#pragma once

// ANAN fork build 380: what the panadapter of a receiver becomes while the
// FTX mode has it (see core/ftx/FtxState.h): a bar with the sub-mode buttons
// and Exit, the waterfall of the decoder's audio (MicronSDR's FtWaterfall,
// ported: a strip 0 .. 4000 Hz, as the Wide Graph of WSJT-X / JTDX), and under
// it the room for the decode windows of the next stages.
//
// Preparation stage: the waterfall's spectrum is made here from the receiver's
// demodulated audio (24 kHz -> 12 kHz, FFT 2048, 4 FFTs half overlapped a
// line: 5.86 Hz, 0.34 s a line -- MicronSDR's figures). The decoders, their
// own demodulator from the receiver's I/Q and the transmitter come with the
// port of FtUnit.

#include <QImage>
#include <QVector>
#include <QWidget>

#include <complex>
#include <vector>

class QLabel;
class QPushButton;

namespace AetherSDR {

class FtxWaterfall : public QWidget {
    Q_OBJECT

public:
    static constexpr int kFft = 2048;
    static constexpr int kBins = kFft / 2;
    static double binHz() { return 12000.0 / kFft; }

    explicit FtxWaterfall(QWidget* parent = nullptr);

    void setRange(int fromHz, int toHz);
    void setRx(int hz) { m_rx = hz; update(); }
    void setTx(int hz) { m_tx = hz; update(); }
    void setSubMode(int mode);
    void clear();
    // One line: kBins power values in dB, 0 .. 6000 Hz; its UTC end time.
    void addLine(const QVector<float>& db, qint64 tMs);

signals:
    void rxClicked(int hz);
    void txClicked(int hz);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;

private:
    double xToHz(int x) const;
    int hzToX(double hz) const;
    void setFreqAt(int x);

    QImage m_img;   // the waterfall without the scale
    int m_f0 = 0, m_f1 = 4000;
    int m_rx = 1500, m_tx = 1500;
    int m_mode = 8;
    double m_bw = 50.0;
    qint64 m_lastPeriod = -1;
    float m_floorDb = 0.0f, m_spanDb = 30.0f;
    bool m_levelInit = false;
    int m_hoverX = -1;
    Qt::MouseButton m_freqDrag = Qt::NoButton;
    QRgb m_pal[256];
    static constexpr int kScalePx = 26;
};

class FtxView : public QWidget {
    Q_OBJECT

public:
    explicit FtxView(QWidget* parent = nullptr);

    // The receiver's demodulated audio: interleaved stereo float, 24 kHz.
    void feedAudio(const QVector<float>& stereo24k);
    void setDialText(const QString& text);

signals:
    void exitRequested();
    void subModeChanged(int mode);

private:
    void pushAudio12k(const float* a, std::size_t n);
    void fftLine();

    FtxWaterfall* m_waterfall = nullptr;
    QLabel* m_dial = nullptr;

    // 24 -> 12 kHz: a half-band FIR (low-pass at a quarter of the input rate).
    std::vector<float> m_decHist;
    std::vector<float> m_decTaps;
    std::size_t m_decPos = 0;
    bool m_decPhase = false;

    // The line's spectrum: FFT 2048, Hann, hop 1024, the power of 4 FFTs.
    std::vector<float> m_buf;          // the last kFft samples
    std::size_t m_bufFill = 0;
    std::vector<float> m_window;
    std::vector<double> m_power;
    int m_ffts = 0;
    std::vector<std::complex<float>> m_work;
};

}  // namespace AetherSDR
