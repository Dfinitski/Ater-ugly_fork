#pragma once

// ANAN fork build 864 (operator, 28.11: "in the Appearance window a new group
// Extra Applets with a button Detach Flag to Applet. Pressed, the current
// receiver's flag is hidden completely and a ready, open applet appears on
// the display. It can be used on the canvas, and the state is remembered
// until the applet is closed; then the flag shows on its receiver again. On
// every receiver except the sub receivers").
//
// The applet's content: a second VfoWidget of the receiver (MainWindow makes
// and wires it as the panadapter's), never collapsed, top-left, its close /
// lock / record / play buttons at its right. The host keeps one size: as
// wide as the flag and its buttons, as tall as the tallest the flag has been
// (its tabs open downwards), so the window does not jump as tabs open and
// close. Without the receiver (not connected yet) it says so.

#include <QPointer>
#include <QWidget>

class QLabel;

namespace AetherSDR {

class SliceModel;
class VfoWidget;

class DetachedFlagApplet : public QWidget {
    Q_OBJECT
public:
    explicit DetachedFlagApplet(int sliceId, QWidget* parent = nullptr);

    [[nodiscard]] int sliceId() const { return m_sliceId; }
    // Build 866: out of line -- QPointer<T>::data() needs T complete (MSVC
    // C2440 with the forward declarations here).
    [[nodiscard]] VfoWidget* flag() const;
    // The receiver the flag shows (nullptr without a flag).
    [[nodiscard]] SliceModel* slice() const;

    // `flag` (of `slice`) was made with this widget as its parent; the host
    // owns it now.
    void adoptFlag(VfoWidget* flag, SliceModel* slice);
    // Deletes the flag (its receiver is gone, or the applet was closed).
    void dropFlag();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refit();

    int m_sliceId = 0;
    QPointer<VfoWidget> m_flag;
    QPointer<SliceModel> m_slice;
    QLabel* m_placeholder = nullptr;
    int m_maxHeight = 0;
};

} // namespace AetherSDR
