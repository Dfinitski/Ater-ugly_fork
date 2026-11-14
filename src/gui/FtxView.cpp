// The FTX view: see FtxView.h.
#include "gui/FtxView.h"

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
    auto* exitBtn = new QPushButton(QStringLiteral("Exit FTX"));
    exitBtn->setFixedHeight(22);
    exitBtn->setFocusPolicy(Qt::NoFocus);
    exitBtn->setToolTip(QStringLiteral(
        "Leave the FTX mode (as closing its window does): the receiver, its panadapter and its sound as before."));
    connect(exitBtn, &QPushButton::clicked, this, &FtxView::exitRequested);
    bar->addWidget(exitBtn);
    m_layout->addLayout(bar);
    m_layout->addStretch(1);   // the waterfall goes above this (setWaterfall)
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
    waterfall->setParent(this);
    waterfall->setFixedHeight(std::clamp(height, 60, 600));
    m_layout->insertWidget(1, waterfall);   // under the bar, above the stretch
    waterfall->show();
}

QWidget* FtxView::takeWaterfall()
{
    QWidget* w = m_waterfall;
    m_waterfall = nullptr;
    if (w) {
        m_layout->removeWidget(w);
        w->hide();
        w->setParent(nullptr);
    }
    return w;
}

void FtxView::setDialText(const QString& text)
{
    m_dial->setText(text);
}

}  // namespace AetherSDR
