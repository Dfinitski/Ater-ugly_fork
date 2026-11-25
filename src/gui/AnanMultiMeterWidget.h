#pragma once

// ANAN fork build 735 (operator, 25.11): the fork's own multi meter, the
// other face of the side panel's S-meter (right click -> Meter -> Multi
// Meter). It replaces the meter of builds 680..734, whose face followed the
// layout of another program's meter images; that design is its author's and
// is not used any more. This one is a linear ("edgewise") meter designed
// here: one horizontal scale per reading -- S, PO, SWR, ALC, COMP (build
// 736: no Id / Vd, the bottom bar shows them) -- with a triangular pointer,
// a bar up to it and the value at the right; all readings at once, those of
// the other direction (receive / transmit) dimmed. The scales, their
// calibration and the drawing are this file's own.
//
// The light face is drawn on the operator's paper background (320 x 240,
// :/images/meter-paper-light.png); that size is also the largest the meter
// grows to (operator: "the resolution the stretched applet can go up to").
// Build 736: on the paper everything is printed in the Multiply mode, opaque
// dark inks, so it reads as paint on the paper (operator: "more contrast,
// less transparency -- it should look like paint on paper").

#include <QWidget>
#include <QTimer>
#include <QPixmap>
#include <QString>
#include <QVector>

class QPainter;

namespace AetherSDR {

class AnanMultiMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit AnanMultiMeterWidget(QWidget* parent = nullptr);

    enum class Face { Light, Dark };
    void setFace(Face face);
    Face face() const { return m_face; }

    // Docked: as wide as the panel (at most kMaxWidth), as high as the meter
    // needs for that width; floating: the largest meter that fits the window,
    // at most kMaxWidth x kMaxHeight, centred.
    void setFloating(bool floating);

    // Build 739 (operator, 25.11: "Shadow TX scales" in the right-click
    // menu): dim the transmit scales on receive, or not.
    void setShadowTxScales(bool on);
    bool shadowTxScales() const { return m_shadowTx; }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // The paper background's size: the meter's design size and its largest.
    static constexpr int kMaxWidth = 320;
    static constexpr int kMaxHeight = 240;

public slots:
    void setLevel(float dbm);                     // RX signal (dBm)
    void setTxMeters(float forwardWatts, float swr);
    void setTransmitting(bool transmitting);
    void setMaxPower(float watts);                // PO scale: 0..1.5 x this
    void setAlcLevel(float dbfs);                 // post-ALC peak level
    // Build 739: the compression the mic applet's gauge shows (dB, >= 0).
    void setCompression(float db);
    void setSwrValid(bool valid);                 // build 738: the radio measured an SWR

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

public:
    // The painting itself, for any target.
    void render(QPainter& p, const QRectF& area);

private:
    enum class Row { S, Po, Swr, Alc, Comp };
    static constexpr int kRows = 5;
    struct Mark { double pos; QString text; };
    struct Scale {
        QVector<Mark> marks;
        double redFrom{2.0};   // 0..1; red above this (2 = none)
        bool reversed{false};  // build 739: the bar fills from the right
    };

    bool rowActive(Row row) const;
    static QString rowName(Row row);
    Scale scaleOf(Row row) const;
    double position(Row row, float value) const;   // 0..1 along the scale
    float reading(Row row) const;
    QString valueText(Row row, float value) const;
    QRectF meterArea(const QRectF& area) const;
    void drawStatic(QPainter& p, const QRectF& meter);
    void drawDynamic(QPainter& p, const QRectF& meter);
    static double rowLineY(int index, double step);
    double valueColumnLeft(Row row) const;
    double nameColumnRight(Row row) const;
    void tick();

    Face m_face{Face::Light};
    bool m_floating{false};
    bool m_transmitting{false};
    QTimer m_tick;

    // Latest readings.
    float m_rxDbm{-127.0f};
    float m_fwdWatts{0.0f};
    float m_swr{1.0f};
    bool m_swrValid{false};
    float m_maxPower{100.0f};
    float m_alcDbfs{-30.0f};
    float m_compDb{0.0f};
    bool m_shadowTx{true};
    // What the pointers show (smoothed), one per Row.
    float m_shown[kRows]{};

    QPixmap m_paper;          // the light face's background
    QPixmap m_staticCache;    // background + scales
    QString m_staticKey;
};

} // namespace AetherSDR
