#pragma once

// ANAN fork build 868 (operator, 28.11: "add two more buttons to our new
// menu -- Extra Band Applet and Extra Mode Applet. The same buttons as the
// bottom panel's elements, in the same style, only scaled to our minimal
// applet size. Pulled out, it could be scaled together with the buttons,
// their sizes").
//
// A grid of `rows` x `cols` buttons drawn as the bottom panel's HAM Bands /
// Modes elements (MainWindow.cpp, builds 829 / 843), filling the applet: in
// the panel at the panel's width; pulled out, the buttons grow and shrink
// with the window, both axes, and their lettering with them. MainWindow gives
// the buttons their text and behaviour (as the bottom panel's).

#include <QVector>
#include <QWidget>

class QPushButton;

namespace AetherSDR {

class ExtraButtonsApplet : public QWidget {
    Q_OBJECT
public:
    ExtraButtonsApplet(const QString& objectName, int rows, int cols,
                       double aspect, QWidget* parent = nullptr);

    [[nodiscard]] const QVector<QPushButton*>& buttons() const { return m_buttons; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyStyle(int fontPx);

    int m_rows = 1;
    int m_cols = 1;
    double m_aspect = 1.6;   // a button's width / height in the panel
    int m_fontPx = 0;
    QVector<QPushButton*> m_buttons;
};

} // namespace AetherSDR
