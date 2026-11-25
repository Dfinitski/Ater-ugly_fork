#pragma once

// ANAN fork build 680 (operator, 24.11: "take the multi meter of Thetis and
// put it in as an option of the S-meter, chosen with the right button on the
// S-meter of the side panel"). The "Anan Multi Meter" of Thetis
// (MeterManager.cs, clsMeter::AddAnanMM, Richard Samphire MW0LGE, GPL v2 or
// later): its needles -- the calibration tables, the needle geometry, the
// attack and decay ratios, the 4 s history, the display groups and the group
// click box -- carried over as they are. The face is drawn here (the operator:
// "draw the scale yourself"), on the layout of Thetis's ananMM images; none of
// those images is used.

#include <QWidget>
#include <QTimer>
#include <QPixmap>
#include <QPointF>
#include <QColor>
#include <QVector>
#include <QPair>
#include <deque>

class QPainter;

namespace AetherSDR {

class AnanMultiMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit AnanMultiMeterWidget(QWidget* parent = nullptr);

    enum class Face { Light, Dark };
    void setFace(Face face);
    Face face() const { return m_face; }

    // Docked: as wide as the panel, as high as the meter needs for that width;
    // floating: fills the window (the meter keeps its proportions, centred).
    void setFloating(bool floating);

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Thetis's display groups: 0 ALL, 1 PWR/SWR, 2 Comp, 3 ALC, 4 Volts/Amps.
    int displayGroup() const { return m_displayGroup; }
    void setDisplayGroup(int group);
    static QString displayGroupName(int group);
    static constexpr int kDisplayGroups = 5;

    // Thetis's face proportions: the meter 1 : 0.441, the strip under it
    // 75/900 of the width.
    static constexpr double kFaceAspect = 0.441;
    static constexpr double kStripAspect = 75.0 / 900.0;

public slots:
    void setLevel(float dbm);                     // RX signal (dBm)
    void setTxMeters(float forwardWatts, float swr);
    void setTransmitting(bool transmitting);
    void setMaxPower(float watts);                // PO scale: 0..1.5 x this
    void setSupplyVolts(float volts);
    void setPaCurrent(float amps);
    void setAlcLevel(float dbfs);                 // post-ALC peak level
    void setAlcGain(float db);                    // ALC gain (0 = unity, < 0 = reducing)

signals:
    void displayGroupChanged(int group);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

public:
    // The painting itself, for any target (also used by the face test).
    void render(QPainter& p, const QRectF& area);

private:
    enum class Source { Signal, Volts, Amps, Power, Swr, AlcComp, AlcGroup };
    struct Needle {
        Source source;
        bool onlyRx{false};
        bool onlyTx{false};
        int group{0};
        float attack{0.8f};
        float decay{0.2f};
        int historyMs{500};
        bool showHistory{false};
        QColor colour;            // empty (invalid) = the face's ink colour
        QColor historyColour;
        float lengthFactor{1.0f};
        QVector<QPair<float, QPointF>> calibration;   // value -> point (0..1 of the face)
        float value{0.0f};
        std::deque<float> history;
        int ignoreNext{0};
    };

    void buildNeedles();
    void tick();
    float reading(Source source) const;
    QPointF calibrationPoint(const Needle& n, float value) const;
    bool needleShown(const Needle& n) const;
    QRectF meterArea(const QRectF& area) const;
    QRectF boxRect(const QRectF& meter) const;
    void drawFace(QPainter& p, const QRectF& face);
    void drawStrip(QPainter& p, const QRectF& strip);
    void drawNeedle(QPainter& p, const QRectF& face, const Needle& n) const;
    void drawPowerLabels(QPainter& p, const QRectF& face) const;

    Face m_face{Face::Light};
    QVector<Needle> m_needles;
    QTimer m_tick;
    int m_displayGroup{0};
    bool m_floating{false};
    bool m_transmitting{false};

    // Latest readings.
    float m_rxDbm{-127.0f};
    float m_fwdWatts{0.0f};
    float m_swr{1.0f};
    float m_maxPower{100.0f};
    float m_volts{0.0f};
    float m_amps{0.0f};
    float m_alcDbfs{-30.0f};
    float m_alcGainDb{0.0f};

    QPixmap m_faceCache;
    QSize m_faceCacheSize;
    qreal m_faceCacheDpr{0.0};
    Face m_faceCacheFace{Face::Light};
    float m_faceCacheMaxPower{0.0f};
};

} // namespace AetherSDR
