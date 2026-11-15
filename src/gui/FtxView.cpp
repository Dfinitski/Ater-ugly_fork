// The FTX view: see FtxView.h.
#include "gui/FtxView.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace AetherSDR {

FtxView::FtxView(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(16, 18, 22));
    setPalette(pal);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->setSpacing(8);
    auto* title = new QLabel(QStringLiteral("FTX"));
    title->setStyleSheet(QStringLiteral("color:#e0e0e0; font-weight:bold;"));
    bar->addWidget(title);
    m_dial = new QLabel;
    m_dial->setStyleSheet(QStringLiteral("color:#c8c8c8;"));
    bar->addWidget(m_dial);
    bar->addStretch(1);
    // Build 385 (operator, 14.11): the side menu with its "+RX" is hidden in
    // FTX, so the button is here -- a second, independent receiver can still
    // be opened beside the mode.
    auto* addRxBtn = new QPushButton(QStringLiteral("+RX"));
    addRxBtn->setFixedHeight(22);
    addRxBtn->setFocusPolicy(Qt::NoFocus);
    addRxBtn->setToolTip(QStringLiteral("Add a receiver (a second, independent panadapter)"));
    connect(addRxBtn, &QPushButton::clicked, this, &FtxView::addRxRequested);
    bar->addWidget(addRxBtn);
    m_addRx = addRxBtn;
    auto* exitBtn = new QPushButton(QStringLiteral("Exit FTX"));
    exitBtn->setFixedHeight(22);
    exitBtn->setFocusPolicy(Qt::NoFocus);
    exitBtn->setToolTip(QStringLiteral(
        "Leave the FTX mode (as closing its window does): the receiver, its panadapter and its sound as before."));
    connect(exitBtn, &QPushButton::clicked, this, &FtxView::exitRequested);
    bar->addWidget(exitBtn);
    m_layout->addLayout(bar);
    // Build 428 (operator, 15.11): the waterfall in a thin frame with small
    // margins, so that its edges are seen.
    m_wfBox = new QWidget(this);
    auto* box = new QVBoxLayout(m_wfBox);
    box->setContentsMargins(6, 2, 6, 6);
    box->setSpacing(0);
    m_wfFrame = new QFrame(m_wfBox);
    m_wfFrame->setObjectName(QStringLiteral("ftxWaterfallFrame"));
    m_wfFrame->setStyleSheet(QStringLiteral(
        "#ftxWaterfallFrame { border: 1px solid #4a5568; border-radius: 3px; background: #101216; }"));
    m_wfLayout = new QVBoxLayout(m_wfFrame);
    m_wfLayout->setContentsMargins(3, 3, 3, 3);
    m_wfLayout->setSpacing(0);
    box->addWidget(m_wfFrame);
    m_wfBox->hide();
    m_layout->addWidget(m_wfBox);
    m_layout->addStretch(1);   // the waterfall's frame goes above this (setWaterfall)
}

FtxView::~FtxView()
{
    takeWaterfall();   // never deleted with this view: it is the FTX window's
}

void FtxView::setWaterfall(QWidget* waterfall, int height)
{
    takeWaterfall();
    if (!waterfall)
        return;
    m_waterfall = waterfall;
    waterfall->setParent(m_wfFrame);
    waterfall->setFixedHeight(std::clamp(height, 60, 600));
    m_wfLayout->addWidget(waterfall);
    // The frame is seen with the waterfall: FtPanel / MainWindow hide and
    // show the waterfall itself (Waterfall off in the FTX window settings).
    waterfall->installEventFilter(this);
    waterfall->show();
    m_wfBox->show();
}

QWidget* FtxView::takeWaterfall()
{
    QWidget* w = m_waterfall;
    m_waterfall = nullptr;
    if (w) {
        w->removeEventFilter(this);
        m_wfLayout->removeWidget(w);
        w->hide();
        w->setParent(nullptr);
    }
    if (m_wfBox)
        m_wfBox->hide();
    return w;
}

bool FtxView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_waterfall && m_wfBox) {
        if (event->type() == QEvent::ShowToParent)
            m_wfBox->show();
        else if (event->type() == QEvent::HideToParent)
            m_wfBox->hide();
    }
    return QWidget::eventFilter(watched, event);
}

void FtxView::setAddRxVisible(bool visible)
{
    if (m_addRx)
        m_addRx->setVisible(visible);
}

void FtxView::setDialText(const QString& text)
{
    m_dial->setText(text);
}

}  // namespace AetherSDR
