// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
// The waterfall window of the FT8 / FT4 / WSPR mode: see FtWaterfall.h
#include "FtWaterfall.h"
#include "FtUnit.h"
#include <QPainter>
#include <QTimer>
#include <QMouseEvent>
#include <QDateTime>
#include <cmath>
#include <cstring>
#include <algorithm>

FtWaterfall::FtWaterfall(FtUnit *u, QWidget *parent)
    : QWidget(parent), unit(u), mode(FtUnit::FT8)
{
    setMinimumSize(200, 60);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setCursor(Qt::CrossCursor);
    // the colours of the waterfall of JTDX: dark blue noise, blue, cyan, the signals up to bright yellow
    static const struct { double t; int r, g, b; } stop[] = {
        { 0.00,   0,   0,  24 }, { 0.20,   0,  12, 100 }, { 0.40,   0,  64, 210 }, { 0.56,   0, 165, 255 },
        { 0.70,   0, 240, 240 }, { 0.84, 170, 255, 110 }, { 1.00, 255, 255,   0 } };     // the strongest: bright yellow
    const int ns = sizeof(stop) / sizeof(stop[0]);
    for(int i = 0; i < 256; i++)
    {
        const double t = i / 255.0;
        int k = 0;
        while(k < ns - 2 && t > stop[k + 1].t)
            k++;
        const double f = (t - stop[k].t) / (stop[k + 1].t - stop[k].t);
        pal[i] = qRgb((int)(stop[k].r + f * (stop[k + 1].r - stop[k].r)), (int)(stop[k].g + f * (stop[k + 1].g - stop[k].g)),
                      (int)(stop[k].b + f * (stop[k + 1].b - stop[k].b)));
    }
    timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, &FtWaterfall::onTimer);
}

void FtWaterfall::setRange(int fromHz, int toHz)
{
    fromHz = qBound(0, fromHz, 5500);
    toHz = qBound(fromHz + 200, toHz, 6000);
    if(fromHz == f0 && toHz == f1)
        return;
    f0 = fromHz;
    f1 = toHz;
    clear();                            // the old lines are of another range
}

void FtWaterfall::setMode(int m)
{
    mode = m;
    bw = (m == FtUnit::FT4) ? 4 * 12000.0 / 576.0 : (m == FtUnit::WSPR) ? 6.0 : 8 * 6.25;
    // AetherSDR, build 409: JS8 -- 8 tones of 12000 / (the samples of a symbol):
    // Normal 1920 (50 Hz), Fast 1200 (80 Hz), Turbo 600 (160 Hz), Slow 3840 (25 Hz)
    switch(m)
    {
        case FtUnit::JS8F: bw = 80.0; break;
        case FtUnit::JS8T: bw = 160.0; break;
        case FtUnit::JS8S: bw = 25.0; break;
        default: break;
    }
    lastPeriod = -1;
    update();
}

void FtWaterfall::setRunning(bool on)
{
    runWanted = on;     // AetherSDR, 395
    applyRun();
}

void FtWaterfall::setPaused(bool on)
{
    if(on == paused)
        return;
    paused = on;
    applyRun();
}

void FtWaterfall::applyRun()
{
    const bool on = runWanted && !paused;
    unit->setSpectrumOn(on);
    if(on)
    {
        unit->takeSpectrum();           // the old ones out
        timer->start();
    }
    else
        timer->stop();
}

void FtWaterfall::clear()
{
    if(!img.isNull())
        img.fill(pal[0]);
    lastPeriod = -1;
    update();
}

double FtWaterfall::xToHz(int x) const
{
    return f0 + (double)x * (f1 - f0) / qMax(1, width());
}

int FtWaterfall::hzToX(double hz) const
{
    return (int)lround((hz - f0) * width() / (double)(f1 - f0));
}

void FtWaterfall::resizeEvent(QResizeEvent *)
{
    const int w = qMax(1, width()), h = qMax(1, height() - SCALE_PX);
    if(img.isNull())
    {
        img = QImage(w, h, QImage::Format_RGB32);
        img.fill(pal[0]);
    }
    else if(img.width() != w || img.height() != h)
    {
        // the history kept: the same lines stretched to the new width, cut / filled at the bottom
        QImage n(w, h, QImage::Format_RGB32);
        n.fill(pal[0]);
        QPainter p(&n);
        const QImage s = img.scaled(w, img.height());
        p.drawImage(0, 0, s);
        img = n;
    }
}

void FtWaterfall::onTimer()
{
    const QVector<FtUnit::SpecLine> lines = unit->takeSpectrum();
    for(const FtUnit::SpecLine &l : lines)
        addLine(l.db, l.tMs);
    if(!lines.isEmpty())
        update();
}

void FtWaterfall::addLine(const QVector<float> &db, qint64 tMs)
{
    if(img.isNull() || db.size() < (int)FtUnit::SPEC_BINS)
        return;
    const int w = img.width(), h = img.height();
    const double hzBin = FtUnit::specHz();
    const int NB = FtUnit::SPEC_BINS;   // (a copy: qBound takes references)
    // the levels by themselves: the noise floor (25 % of the bins shown) and the span of the signals
    const int k0 = qBound(0, (int)(f0 / hzBin), NB - 1);
    const int k1 = qBound(k0 + 1, (int)ceil(f1 / hzBin), NB);
    std::vector<float> v(db.constBegin() + k0, db.constBegin() + k1);
    std::nth_element(v.begin(), v.begin() + v.size() / 4, v.end());
    const float fl = v[v.size() / 4];
    std::nth_element(v.begin(), v.begin() + (v.size() * 995) / 1000, v.end());
    const float top = v[(v.size() * 995) / 1000];
    const float span = qBound(20.0f, top - fl + 4.0f, 50.0f);
    if(!levelInit)
    {
        floorDb = fl;
        spanDb = span;
        levelInit = true;
    }
    else
    {
        floorDb += 0.15f * (fl - floorDb);                          // about 2 s
        spanDb += (span > spanDb ? 0.05f : 0.01f) * (span - spanDb);  // a strong signal: up faster
    }
    // the image one line down, the new line at the top
    uchar *bits = img.bits();
    const int bpl = img.bytesPerLine();
    memmove(bits + bpl, bits, (size_t)bpl * (h - 1));
    QRgb *row = (QRgb *)bits;
    const float lo = floorDb - 3.0f, k = 1.0f / (spanDb + 3.0f);   // the noise: dark blue
    for(int x = 0; x < w; x++)
    {
        const double ha = xToHz(x) / hzBin, hb = xToHz(x + 1) / hzBin;
        float val;
        if(hb - ha >= 1.0)
        {
            // several bins in a pixel: the strongest
            const int a = qBound(0, (int)floor(ha), NB - 1), b = qBound(a, (int)ceil(hb) - 1, NB - 1);
            val = db[a];
            for(int i = a + 1; i <= b; i++)
                val = std::max(val, db[i]);
        }
        else
        {
            // less than a bin: between the two nearest
            const double c = (ha + hb) / 2.0 - 0.5;
            const int a = qBound(0, (int)floor(c), NB - 2);
            const float f = (float)qBound(0.0, c - a, 1.0);
            val = db[a] + f * (db[a + 1] - db[a]);
        }
        const int i = qBound(0, (int)((val - lo) * k * 255.0f), 255);
        row[x] = pal[i];
    }
    // the start of a period: a line and its UTC
    const int P = FtUnit::periodMs(mode);
    const qint64 per = tMs / P;
    if(lastPeriod >= 0 && per != lastPeriod)
    {
        for(int x = 0; x < w; x += 2)
            row[x] = qRgb(150, 150, 150);
        QPainter p(&img);
        QFont f = font();
        f.setPointSize(7);
        p.setFont(f);
        p.setPen(QColor(220, 220, 220));
        p.drawText(3, 10, QDateTime::fromMSecsSinceEpoch(per * P, Qt::UTC).toString(mode == FtUnit::WSPR ? "HH:mm" : "HH:mm:ss"));
    }
    lastPeriod = per;
}

void FtWaterfall::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int w = width();
    // the scale
    p.fillRect(0, 0, w, SCALE_PX, QColor(28, 28, 28));
    const double pxHz = w / (double)(f1 - f0);
    int step = 100;
    for(int s : { 100, 200, 250, 500, 1000 })
    {
        step = s;
        if(s * pxHz >= 55)
            break;
    }
    const int minor = (step == 250) ? 50 : step / 5 >= 20 ? step / 5 : step / 2;
    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);
    for(int hz = (f0 / minor) * minor; hz <= f1; hz += minor)
    {
        if(hz < f0)
            continue;
        const int x = hzToX(hz);
        const bool major = (hz % step) == 0;
        p.setPen(QColor(150, 150, 150));
        p.drawLine(x, SCALE_PX - 1, x, SCALE_PX - (major ? 9 : 5));
        if(major)
        {
            p.setPen(QColor(220, 220, 220));
            const QString t = QString::number(hz);
            const int tw = p.fontMetrics().horizontalAdvance(t);
            p.drawText(qBound(1, x - tw / 2, w - tw - 1), 11, t);     // the first and the last one inside
        }
    }
    // the search range of the decoder
    p.fillRect(QRect(QPoint(hzToX(sa), SCALE_PX - 3), QPoint(hzToX(sb), SCALE_PX - 2)), QColor(80, 110, 190));
    // the band of the TX signal (red, above) and of the RX one (green)
    auto band = [&](int hz, int y, const QColor &c) {
        const int xa = hzToX(hz), xb = std::max(xa + 2, hzToX(hz + bw));
        p.setPen(QPen(c, 2));
        p.drawLine(xa, y, xb, y);
        p.drawLine(xa, y - 3, xa, y + 1);
        p.drawLine(xb, y - 3, xb, y + 1);
    };
    if(!single)
        band(tx, 15, QColor(255, 60, 60));
    band(rx, 20, QColor(60, 230, 60));
    // the waterfall
    if(!img.isNull())
        p.drawImage(0, SCALE_PX, img);
    // the RX and TX bands: two lines each, the start and the end of the signal
    if(!single)
    {
        p.setPen(QPen(QColor(255, 60, 60), 1));
        p.drawLine(hzToX(tx), SCALE_PX, hzToX(tx), height());
        p.drawLine(hzToX(tx + bw), SCALE_PX, hzToX(tx + bw), height());
    }
    p.setPen(QPen(QColor(60, 230, 60), 1));
    p.drawLine(hzToX(rx), SCALE_PX, hzToX(rx), height());
    p.drawLine(hzToX(rx + bw), SCALE_PX, hzToX(rx + bw), height());
    // the frequency under the mouse
    if(hoverX >= 0)
    {
        const QString t = QString("%1 Hz").arg(qRound(xToHz(hoverX)));
        const int tw = p.fontMetrics().horizontalAdvance(t) + 8;
        int x = hoverX + 6;
        if(x + tw > w)
            x = hoverX - 6 - tw;
        p.fillRect(x, SCALE_PX + 2, tw, 15, QColor(0, 0, 0, 170));
        p.setPen(Qt::white);
        p.drawText(QRect(x, SCALE_PX + 2, tw, 15), Qt::AlignCenter, t);
        p.setPen(QPen(QColor(255, 255, 255, 90), 1, Qt::DotLine));
        p.drawLine(hoverX, SCALE_PX, hoverX, height());
    }
}

void FtWaterfall::mousePressEvent(QMouseEvent *e)
{
    if(e->button() == Qt::LeftButton && e->pos().y() < SCALE_PX)
    {
        // the scale: the height of the strip dragged
        dragY0 = e->globalPos().y();
        dragH0 = height();
        return;
    }
    if(e->button() == Qt::LeftButton || e->button() == Qt::RightButton)
    {
        freqDrag = e->button();         // held: the band follows the mouse
        setFreqAt(e->pos().x());
    }
}

// the RX (left button) / TX (right button) band centred on x
void FtWaterfall::setFreqAt(int x)
{
    const int hz = qBound(f0, (int)lround(xToHz(x) - bw / 2.0), (int)(f1 - bw));
    if((freqDrag == Qt::LeftButton || single) && hz != rx)
    {
        rx = hz;
        emit rxClicked(hz);
    }
    else if(!single && freqDrag == Qt::RightButton && hz != tx)
    {
        tx = hz;
        emit txClicked(hz);
    }
    update();
}

void FtWaterfall::mouseMoveEvent(QMouseEvent *e)
{
    if(dragY0 >= 0)
    {
        emit heightRequested(dragH0 - (e->globalPos().y() - dragY0));   // up: higher
        return;
    }
    hoverX = e->pos().x();
    if(freqDrag != Qt::NoButton)
    {
        setFreqAt(qBound(0, e->pos().x(), width() - 1));
        return;
    }
    setCursor(e->pos().y() < SCALE_PX ? Qt::SizeVerCursor : Qt::CrossCursor);
    update();
}

void FtWaterfall::mouseReleaseEvent(QMouseEvent *)
{
    dragY0 = -1;
    freqDrag = Qt::NoButton;
}

void FtWaterfall::leaveEvent(QEvent *)
{
    if(dragY0 >= 0 || freqDrag != Qt::NoButton)
        return;
    hoverX = -1;
    update();
}
