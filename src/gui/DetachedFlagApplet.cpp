#include "DetachedFlagApplet.h"

#include "VfoWidget.h"
#include "models/SliceModel.h"

#include <QEvent>
#include <QLabel>
#include <QTimer>

#include <algorithm>

namespace AetherSDR {

namespace {
// The buttons beside the flag (VfoWidget::placeForApplet): 20 px and a 2 px
// gap, four of them stacked.
constexpr int kButtonColumn = 2 + 20 + 2;
constexpr int kButtonsHeight = 4 * 20 + 3 * 2;
constexpr int kPlaceholderW = 260;
constexpr int kPlaceholderH = 60;
} // namespace

DetachedFlagApplet::DetachedFlagApplet(int sliceId, QWidget* parent)
    : QWidget(parent)
    , m_sliceId(sliceId)
{
    setObjectName(QStringLiteral("detachedFlagApplet%1").arg(sliceId));
    m_placeholder = new QLabel(this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->setText(QStringLiteral("Receiver %1: its flag appears here when the receiver is on.")
                               .arg(QChar(QLatin1Char('A').unicode() + sliceId)));
    m_placeholder->setGeometry(0, 0, kPlaceholderW, kPlaceholderH);
    setFixedSize(kPlaceholderW, kPlaceholderH);
}

VfoWidget* DetachedFlagApplet::flag() const
{
    return m_flag.data();
}

SliceModel* DetachedFlagApplet::slice() const
{
    return m_slice.data();
}

void DetachedFlagApplet::adoptFlag(VfoWidget* flag, SliceModel* slice)
{
    if (m_flag == flag)
        return;
    dropFlag();
    m_flag = flag;
    m_slice = slice;
    if (!flag)
        return;
    if (flag->parentWidget() != this)
        flag->setParent(this);
    flag->installEventFilter(this);
    m_placeholder->hide();
    flag->show();
    refit();
    // The flag settles its layout after its first show.
    QTimer::singleShot(0, this, [this] { refit(); });
}

void DetachedFlagApplet::dropFlag()
{
    m_slice = nullptr;
    if (VfoWidget* f = m_flag.data()) {
        m_flag = nullptr;
        f->removeEventFilter(this);
        delete f;   // its buttons and shadow go with it (~VfoWidget)
    }
    m_placeholder->show();
    m_maxHeight = 0;
    m_placeholder->setGeometry(0, 0, kPlaceholderW, kPlaceholderH);
    setFixedSize(kPlaceholderW, kPlaceholderH);
}

bool DetachedFlagApplet::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_flag.data()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show
            || event->type() == QEvent::Move)) {
        // Queued: the flag is in the middle of its own relayout.
        QTimer::singleShot(0, this, [this] { refit(); });
    }
    return QWidget::eventFilter(watched, event);
}

void DetachedFlagApplet::refit()
{
    VfoWidget* f = m_flag.data();
    if (!f)
        return;
    f->placeForApplet();
    // Build 867 (operator, 28.11: "the window widens as the buttons are used
    // and does not shrink back when they are switched off -- make it follow
    // the content"): the flag's own height now (not the tallest it has
    // been), never less than its button column. A floating window follows
    // (FloatingContainerWindow fits to its content on every layout request).
    m_maxHeight = std::max(f->height(), kButtonsHeight);
    const QSize want(f->width() + kButtonColumn, m_maxHeight);
    if (size() != want)
        setFixedSize(want);
}

} // namespace AetherSDR
