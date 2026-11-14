#pragma once

// ANAN fork build 380/381: what the panadapter of a receiver becomes while the
// FTX mode has it (core/ftx/FtxState.h): a thin bar (the mode, the dial, Exit)
// and the waterfall of the mode -- MicronSDR's FtWaterfall, which belongs to
// the FTX window (ftx/FtPanel) and is lent to this view while the mode is on,
// as MicronSDR puts it under its own panorama. Everything else of the mode is
// in that window.

#include <QPointer>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace AetherSDR {

class FtxView : public QWidget {
    Q_OBJECT

public:
    explicit FtxView(QWidget* parent = nullptr);
    ~FtxView() override;

    // The mode's waterfall: shown under the bar at `height` px. The widget
    // stays the caller's: takeWaterfall() hands it back (parentless, hidden)
    // before this view goes.
    void setWaterfall(QWidget* waterfall, int height);
    QWidget* takeWaterfall();
    void setDialText(const QString& text);

signals:
    void exitRequested();
    void addRxRequested();   // build 385: the "+RX" of the side menu (hidden in FTX)

private:
    QVBoxLayout* m_layout = nullptr;
    QLabel* m_dial = nullptr;
    QPointer<QWidget> m_waterfall;
};

}  // namespace AetherSDR
