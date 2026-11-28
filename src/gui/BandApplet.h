#pragma once

// ANAN fork build 858 (operator, 27.11: "several users want a band selection
// applet; put it on the GHE applet's button. It looks like the panadapter's
// Band panel, but stretched, the buttons rearrange themselves smartly,
// keeping the grouping and the order").
//
// The panadapter Band panel's buttons (SpectrumOverlayMenu::buildBandPanel,
// "overlay.band" style, 48 x 26) in three groups: the amateur bands 160 ... 6,
// WWV / GEN, 2200 / 630 / XVTR. The groups keep their order and their
// buttons theirs; each group fills a row left to right and wraps to the next
// one, and a small group goes on the same row as the one before it when it
// fits there whole, one empty cell after it. The columns follow the width
// (as many 48 px buttons as fit, at least three); the buttons share the
// spare width. At three columns it is exactly the Band panel.

#include <QWidget>

#include <functional>

class QPushButton;
class QTimer;

namespace AetherSDR {

class BandApplet : public QWidget {
    Q_OBJECT
public:
    explicit BandApplet(QWidget* parent = nullptr);

    // The active receiver's frequency in MHz, or < 0 when there is none
    // (not connected): polled every 250 ms for the lit band.
    void setFrequencySource(std::function<double()> source);

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void bandSelected(const QString& bandName, double freqMhz, const QString& mode);
    void xvtrSetupRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Button {
        QPushButton* btn = nullptr;
        QString band;      // "" for XVTR
        int group = 0;
    };
    // Cell (row, column) of every button for `cols` columns; returns the rows.
    int place(int cols, QVector<QPoint>* cells) const;
    int columnsFor(int width) const;
    void relayout();
    void updateLit();

    QVector<Button> m_buttons;
    std::function<double()> m_freqSource;
    QTimer* m_poll = nullptr;
    int m_lastHeight = -1;
};

} // namespace AetherSDR
