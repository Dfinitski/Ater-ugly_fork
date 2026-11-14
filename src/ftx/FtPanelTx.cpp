// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
// The transmitter of the FTX window (FT8 / FT4 / WSPR): the messages, the sequence of a QSO, the
// timing. See FtPanel.h; the waveform: FtTx; the channel symbols: FtUnit::requestEncode (libftx).
//
//   - Tx1 .. Tx6 as WSJT-X: Tx1 "DX MY GRID", Tx2 "DX MY -12", Tx3 "DX MY R-12", Tx4 "DX MY RR73",
//     Tx5 "DX MY 73", Tx6 "CQ MY GRID" (editable; Generate: made again). The button of a message:
//     the next one to send. WSPR: one message "MY GRID dBm" (Tx6), sent in a part (TX %) of the
//     2 min periods, at random as WSJT-X.
//   - Enable TX: sent in each period of its parity (1st: 00 / 30 s of FT8, the even ones) from 0.5 s
//     (WSPR: 1 s of the even minute); late by up to 2 s (FT4 1 s): the waveform from that point.
//     Halt TX: off now. Enable TX off during a transmission: it ends first. The watchdog: no
//     action of the user for its minutes - Enable TX off.
//   - A double click on a decode: the station called (CQ, a QSO of others: Tx1; a message to me:
//     the answer to it), the TX period the other one of the station, RX to its frequency (TX too,
//     unless Hold TX freq); Enable TX on.
//   - Auto seq: the messages to me from the DX (or anybody answering my CQ) choose the next one
//     (grid -> Tx2, report -> Tx3, R+report -> Tx4, RRR / RR73 -> Tx5, 73 -> done); nothing from
//     the DX: the same again. 73 sent (Tx5): Enable TX off (Disable TX after 73); RR73 sent: off
//     when the DX sends no R+report again.
//   - The periods of my transmissions are not decoded (no own signal, no spots of it).
//   - The log (FtNet.h: wsjtx_log.adi, as JTDX): a QSO is logged when RR73 or 73 is sent (once),
//     without a dialog: the reports, the grid, the times, the frequency, the power of the meter.
//     QRZ: the QSOs of the log not sent yet to the QRZ.com logbook (its API key in the settings).
#include "FtPanel.h"
#include <QtWidgets>
#include <QSettings>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>

QString FtPanel::rpt(int db)
{
    db = qBound(-50, db, 49);
    return QString("%1%2").arg(db >= 0 ? "+" : "-").arg(qAbs(db), 2, 10, QChar('0'));
}

void FtPanel::buildTxUi(QVBoxLayout *v)
{
    wTx = new QWidget;
    QGridLayout *g = new QGridLayout(wTx);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(4);
    g->setVerticalSpacing(3);
    QHBoxLayout *h = new QHBoxLayout;
    h->setSpacing(4);
    lbDxLbl = new QLabel("DX");
    h->addWidget(lbDxLbl);
    leDx = new QLineEdit;
    leDx->setFixedWidth(100);    // AetherSDR: this program's font
    leDx->setMaxLength(12);
    leDx->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9/<>]{0,14}"), leDx));
    leDx->setToolTip("The station called (a double click on its decode puts it here)");
    h->addWidget(leDx);
    lbGridLbl = new QLabel("Grid");
    h->addWidget(lbGridLbl);
    leDxGrid = new QLineEdit;
    leDxGrid->setFixedWidth(66);
    leDxGrid->setMaxLength(6);
    h->addWidget(leDxGrid);
    lbRptLbl = new QLabel("Rpt");
    h->addWidget(lbRptLbl);
    sbRpt = new QSpinBox;
    sbRpt->setRange(-50, 49);
    sbRpt->setValue(-10);
    sbRpt->setFixedWidth(60);
    sbRpt->setToolTip("The report sent (dB): the SNR of the DX, put by the double click and Auto seq");
    h->addWidget(sbRpt);
    pbGen = new QPushButton("Gen");
    pbGen->setFocusPolicy(Qt::NoFocus);
    pbGen->setToolTip("The messages Tx1 .. Tx6 made again (the DX, its grid, the report, my call and grid)");
    h->addWidget(pbGen);
    h->addSpacing(10);
    pbEnable = new QPushButton("Enable TX");
    pbEnable->setCheckable(true);
    pbEnable->setFocusPolicy(Qt::NoFocus);
    pbEnable->setStyleSheet("QPushButton { padding: 2px 5px; } QPushButton:checked { background: #A02020; border-color: #FF8080; }");
    pbEnable->setToolTip("Transmit in the periods of the TX parity (TX 1st), the message selected");
    h->addWidget(pbEnable);
    pbHalt = new QPushButton("Halt TX");
    pbHalt->setFocusPolicy(Qt::NoFocus);
    pbHalt->setStyleSheet("QPushButton { padding: 2px 5px; }");
    pbGen->setStyleSheet("QPushButton { padding: 2px 5px; }");
    pbHalt->setToolTip("The transmission off now, Enable TX off");
    h->addWidget(pbHalt);
    chbTx1st = new QCheckBox("TX 1st");
    chbTx1st->setToolTip("Transmit in the 1st (even) periods: FT8 00 / 30 s, FT4 00 / 15 / 30 / 45 s;\n"
                         "off: the 2nd (odd) ones. A double click on a decode sets the other one of the station");
    h->addWidget(chbTx1st);
    chbAutoSeq = new QCheckBox("Auto seq");
    chbAutoSeq->setToolTip("The answers of the DX choose the next message (as WSJT-X)");
    h->addWidget(chbAutoSeq);
    h->addStretch(1);
    lbTxState = new QLabel;
    lbTxState->setMinimumWidth(60);
    h->addWidget(lbTxState);
    g->addLayout(h, 0, 0, 1, 6);

    bgTx = new QButtonGroup(this);
    bgTx->setExclusive(true);
    QFont mf("Consolas");
    mf.setStyleHint(QFont::Monospace);
    mf.setPointSize(9);
    for(int i = 0; i < 6; i++)
    {
        rbTx[i] = new QPushButton(QString("Tx%1").arg(i + 1));
        rbTx[i]->setCheckable(true);
        rbTx[i]->setFocusPolicy(Qt::NoFocus);
        rbTx[i]->setFixedWidth(46);
        rbTx[i]->setStyleSheet("QPushButton { padding: 1px 2px; } QPushButton:checked { background: #006040; color: #00ff88; border-color: #00a060; }");
        bgTx->addButton(rbTx[i], i);
        leTx[i] = new QLineEdit;
        leTx[i]->setMaxLength(37);
        leTx[i]->setFont(mf);
        g->addWidget(rbTx[i], 1 + i / 3, (i % 3) * 2);
        g->addWidget(leTx[i], 1 + i / 3, (i % 3) * 2 + 1);
        connect(leTx[i], &QLineEdit::textEdited, this, [this, i](const QString &t) {
            const int c = leTx[i]->cursorPosition();
            leTx[i]->setText(t.toUpper());
            leTx[i]->setCursorPosition(c);
            userAction();
        });
        connect(leTx[i], &QLineEdit::editingFinished, this, [this, i]() {
            Enc e;
            encodedNow(leTx[i]->text().simplified(), e);      // ready before its period
        });
    }
    rbTx[0]->setToolTip("Tx1: DX MyCall MyGrid - calling the DX");
    rbTx[1]->setToolTip("Tx2: DX MyCall report - the answer to its call");
    rbTx[2]->setToolTip("Tx3: DX MyCall R+report");
    rbTx[3]->setToolTip("Tx4: DX MyCall RR73");
    rbTx[4]->setToolTip("Tx5: DX MyCall 73");
    rbTx[5]->setToolTip("Tx6: CQ MyCall MyGrid (WSPR: MyCall MyGrid dBm)");
    for(int c = 1; c < 6; c += 2)
        g->setColumnStretch(c, 1);
    v->addWidget(wTx);

    connect(bgTx, &QButtonGroup::idClicked, this, [this](int i) { selectTx(i); userAction(); });
    connect(pbGen, &QPushButton::clicked, this, [this]() { genMessages(); userAction(); });
    connect(leDx, &QLineEdit::textEdited, this, [this](const QString &t) {
        const int c = leDx->cursorPosition();
        leDx->setText(t.toUpper());
        leDx->setCursorPosition(c);
    });
    connect(leDxGrid, &QLineEdit::textEdited, this, [this](const QString &t) {
        const int c = leDxGrid->cursorPosition();
        leDxGrid->setText(t.left(2).toUpper() + t.mid(2, 2) + t.mid(4).toLower());
        leDxGrid->setCursorPosition(c);
    });
    for(QLineEdit *e : { leDx, leDxGrid })
        connect(e, &QLineEdit::editingFinished, this, [this]() { genMessages(); userAction(); });
    connect(sbRpt, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { genMessages(); });
    connect(pbEnable, &QPushButton::toggled, this, [this](bool on) {
        if(on)
        {
            userAction();
            if(!unit->hasEncoder())
            {
                lbStat->setText(unit->error().isEmpty() ? QString("libftx.dll is older than 1.20: no transmitter (build FT_src again)")
                                                        : unit->error());
                pbEnable->setChecked(false);
                return;
            }
            if(leCall->text().trimmed().isEmpty() || leGrid->text().trimmed().size() < 4)
            {
                lbStat->setText("TX: my call and locator are needed (Settings)");
                pbEnable->setChecked(false);
                return;
            }
            Enc e;
            encodedNow(currentTxMsg(), e);
        }
        txUi();
    });
    connect(pbHalt, &QPushButton::clicked, this, [this]() { haltTx(); });
    connect(chbTx1st, &QCheckBox::toggled, this, [this]() { userAction(); });
    connect(unit, &FtUnit::encoded, this, &FtPanel::onEncoded, Qt::QueuedConnection);

    txTick = new QTimer(this);
    txTick->setInterval(40);
    connect(txTick, &QTimer::timeout, this, &FtPanel::onTxTick);
}

void FtPanel::addTxSettings(QGridLayout *g, int row)
{
    chbHoldTx = new QCheckBox("Hold TX freq");
    chbHoldTx->setToolTip("A double click on a decode moves the RX frequency only (off: the TX frequency too)");
    g->addWidget(chbHoldTx, row, 0, 1, 2);
    chbOff73 = new QCheckBox("Disable TX after 73");
    chbOff73->setToolTip("Enable TX off when 73 (Tx5) is sent, and after RR73 (Tx4) when the DX does not ask again");
    g->addWidget(chbOff73, row, 2, 1, 2);
    g->addWidget(new QLabel("TX watchdog, min:"), row + 1, 0);
    sbWatchdog = new QSpinBox;
    sbWatchdog->setRange(0, 60);
    sbWatchdog->setFixedWidth(70);
    sbWatchdog->setToolTip("Enable TX off after so many minutes without an action of the user (0: never)");
    g->addWidget(sbWatchdog, row + 1, 1);
    g->addWidget(new QLabel("WSPR: dBm, TX %:"), row + 1, 2);
    QHBoxLayout *h = new QHBoxLayout;
    sbWsprPwr = new QSpinBox;
    sbWsprPwr->setRange(0, 60);
    sbWsprPwr->setFixedWidth(60);
    sbWsprPwr->setToolTip("WSPR: the power in the message, dBm (37 = 5 W); WSPR takes 0, 3, 7, 10, ...");
    sbWsprPct = new QSpinBox;
    sbWsprPct->setRange(0, 100);
    sbWsprPct->setFixedWidth(60);
    sbWsprPct->setToolTip("WSPR: the part of the 2 min periods transmitted, % (at random, as WSJT-X; 20 %: one in 5)");
    h->addWidget(sbWsprPwr);
    h->addWidget(sbWsprPct);
    h->addStretch(1);
    g->addLayout(h, row + 1, 3);
    connect(sbWsprPwr, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { genMessages(); });
}

void FtPanel::loadTxSettings(QSettings &st)
{
    chbTx1st->setChecked(st.value("Tx1st", false).toBool());
    chbAutoSeq->setChecked(st.value("AutoSeq", true).toBool());
    chbHoldTx->setChecked(st.value("HoldTx", true).toBool());
    chbOff73->setChecked(st.value("DisableTxAfter73", true).toBool());
    sbWatchdog->setValue(st.value("TxWatchdogMin", 6).toInt());
    sbWsprPwr->setValue(st.value("WsprDbm", 37).toInt());
    sbWsprPct->setValue(st.value("WsprTxPct", 20).toInt());
    qrzUi();
    const int sel = qBound(0, st.value("TxNext", 5).toInt(), 5);
    genMessages();
    selectTx(sel);
    txUi();
}

void FtPanel::saveTxSettings(QSettings &st)
{
    st.setValue("Tx1st", chbTx1st->isChecked());
    st.setValue("AutoSeq", chbAutoSeq->isChecked());
    st.setValue("HoldTx", chbHoldTx->isChecked());
    st.setValue("DisableTxAfter73", chbOff73->isChecked());
    st.setValue("TxWatchdogMin", sbWatchdog->value());
    st.setValue("WsprDbm", sbWsprPwr->value());
    st.setValue("WsprTxPct", sbWsprPct->value());
    st.setValue("TxNext", qMax(0, bgTx->checkedId()));
}

// WSPR: one message (Tx6), no QSO; FT8 / FT4: all
void FtPanel::txUi()
{
    const bool w = (curMode == FtUnit::WSPR);
    for(QWidget *x : std::initializer_list<QWidget *>{ lbDxLbl, leDx, lbGridLbl, leDxGrid, lbRptLbl, sbRpt, chbTx1st, chbAutoSeq })
        x->setVisible(!w);
    for(int i = 0; i < 5; i++)
    {
        rbTx[i]->setVisible(!w);
        leTx[i]->setVisible(!w);
    }
    if(w && bgTx->checkedId() != 5)
        rbTx[5]->setChecked(true);
    if(txOn)
    {
        lbTxState->setText("TX: " + txMsgNow);
        lbTxState->setStyleSheet("color: white; background: #A02020; padding: 1px 4px;");
    }
    else if(pbEnable->isChecked())
    {
        lbTxState->setText(curMode == FtUnit::WSPR ? QString("TX on (%1 %)").arg(sbWsprPct->value())
                                                   : QString("TX on: %1").arg(chbTx1st->isChecked() ? "1st" : "2nd"));
        lbTxState->setStyleSheet("color: #FF9090;");
    }
    else
    {
        lbTxState->setText("RX");
        lbTxState->setStyleSheet("color: #90E090;");
    }
}

void FtPanel::genMessages()
{
    const QString my = leCall->text().trimmed().toUpper();
    const QString g4 = leGrid->text().trimmed().left(4).toUpper();
    const QString dx = leDx->text().trimmed().toUpper();
    const QString r = rpt(sbRpt->value());
    if(curMode == FtUnit::WSPR)
    {
        // WSPR takes the powers 0, 3, 7, 10, 13, ... dBm (the last digit 0, 3 or 7)
        int p = sbWsprPwr->value();
        const int d = p % 10;
        p = p - d + (d < 2 ? 0 : d < 5 ? 3 : d < 9 ? 7 : 10);
        leTx[5]->setText(QString("%1 %2 %3").arg(my, g4).arg(p));
    }
    else
    {
        const QString pre = dx.isEmpty() ? QString() : dx + " " + my + " ";
        leTx[0]->setText(dx.isEmpty() ? QString() : pre + g4);
        leTx[1]->setText(dx.isEmpty() ? QString() : pre + r);
        leTx[2]->setText(dx.isEmpty() ? QString() : pre + "R" + r);
        leTx[3]->setText(dx.isEmpty() ? QString() : pre + "RR73");
        leTx[4]->setText(dx.isEmpty() ? QString() : pre + "73");
        leTx[5]->setText(QString("CQ %1 %2").arg(my, g4));
    }
    Enc e;
    encodedNow(currentTxMsg(), e);
}

void FtPanel::selectTx(int i)
{
    if(i < 0 || i > 5)
        return;
    if(!rbTx[i]->isChecked())
        rbTx[i]->setChecked(true);
    if(i == 5 || i == 0)
        rr73Sent = false;               // a new QSO
    Enc e;
    encodedNow(currentTxMsg(), e);
}

QString FtPanel::currentTxMsg() const
{
    const int i = bgTx->checkedId();
    return i < 0 ? QString() : leTx[i]->text().simplified();
}

void FtPanel::userAction()
{
    lastAction = QDateTime::currentMSecsSinceEpoch();
}

void FtPanel::txNote(const QString &text)
{
    lbStat->setText(text);
}

bool FtPanel::encodedNow(const QString &msg, Enc &e)
{
    if(msg.isEmpty() || !unit->hasEncoder())
        return false;
    const QString k = encKey(curMode, msg);
    auto it = encCache.find(k);
    if(it != encCache.end())
    {
        e = it.value();
        return true;
    }
    if(!encPending.contains(k))
    {
        encPending.insert(k);
        unit->requestEncode(curMode, msg);
    }
    return false;
}

void FtPanel::onEncoded(int mode, QString msg, QVector<int> tones, QString sent)
{
    const QString k = encKey(mode, msg);
    encPending.remove(k);
    if(encCache.size() > 200)
        encCache.clear();
    encCache.insert(k, Enc { tones, sent });
    if(tones.isEmpty() && mode == curMode && msg == currentTxMsg())
        lbStat->setText(QString("TX: \"%1\" can not be sent (not a valid message)").arg(msg));
}

std::vector<float> FtPanel::waveOf(const Enc &e) const
{
    if(curMode == FtUnit::FT8 && e.tones.size() == 79)
        return FtTx::ft8Wave(e.tones.constData(), txFreq);
    if(curMode == FtUnit::FT4 && e.tones.size() == 103)
        return FtTx::ft4Wave(e.tones.constData(), txFreq);
    if(curMode == FtUnit::WSPR && e.tones.size() == 162)
    {
        unsigned char s[162];
        for(int i = 0; i < 162; i++)
            s[i] = (unsigned char)e.tones[i];
        return FtTx::wsprWave(s, txFreq + 3.0);     // the band txFreq .. txFreq + 6 Hz: its centre
    }
    return std::vector<float>();
}

// the transmitter: every 40 ms while FTX runs
void FtPanel::onTxTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int P = FtUnit::periodMs(curMode);
    if(txOn)
    {
        // a message changed just after the start (Auto seq on a late decode, a click): the new one
        // from the same point of time
        const QString m = currentTxMsg();
        if(m != txMsgNow && now - txIdx * P < txStartOffMs() + 1500)
        {
            Enc e;
            if(encodedNow(m, e) && !e.tones.isEmpty())
            {
                std::vector<float> w = waveOf(e);
                if(!w.empty())
                {
                    ftx.arm(std::move(w), txIdx * P + txStartOffMs());
                    txMsgNow = m;
                    txSent = bgTx->checkedId();
                    addTxRow(e.sent, txIdx * P);
                    txUi();
                    emit txRearmed();
                }
            }
        }
        if(ftx.done() || now > ftx.startMs() + ftx.lengthMs() + 1500)
            finishTx();
        return;
    }
    if(!pbEnable->isChecked())
        return;
    if(sbWatchdog->value() > 0 && now - lastAction > sbWatchdog->value() * 60000LL)
    {
        pbEnable->setChecked(false);
        lbStat->setText(QString("TX watchdog: %1 min without an action - Enable TX off").arg(sbWatchdog->value()));
        return;
    }
    const qint64 idx = now / P;
    const qint64 off = now - idx * P;
    if(idx == lastTxIdx)
        return;
    if(curMode == FtUnit::WSPR)
    {
        if(wsprDecidedIdx != idx)
        {
            wsprDecidedIdx = idx;
            wsprTxThis = (int)QRandomGenerator::global()->bounded(100) < sbWsprPct->value();
        }
        if(!wsprTxThis)
            return;
    }
    else if((idx % 2 == 0) != chbTx1st->isChecked())
        return;
    if(off > txStartOffMs() + txLateMs())
        return;                         // too late for this one
    Enc e;
    const QString m = currentTxMsg();
    if(m.isEmpty())
    {
        lbStat->setText("TX: no message (the DX call?)");
        return;
    }
    if(!encodedNow(m, e))
        return;                         // asked for: the next tick
    if(e.tones.isEmpty())
    {
        pbEnable->setChecked(false);
        lbStat->setText(QString("TX: \"%1\" can not be sent (not a valid message) - Enable TX off").arg(m));
        return;
    }
    startTx(idx);
}

void FtPanel::startTx(qint64 idx)
{
    const int P = FtUnit::periodMs(curMode);
    const QString m = currentTxMsg();
    Enc e = encCache.value(encKey(curMode, m));
    std::vector<float> w = waveOf(e);
    if(w.empty())
        return;
    ftx.arm(std::move(w), idx * P + txStartOffMs());
    ftx.start();
    txOn = true;
    txIdx = lastTxIdx = idx;
    txSent = (curMode == FtUnit::WSPR) ? -1 : bgTx->checkedId();
    txMsgNow = m;
    if(txSent >= 0 && txSent < 5)
    {
        const QString dx = leDx->text().trimmed().toUpper();
        if(!dx.isEmpty() && qso.call != dx)
            startQso(dx, leDxGrid->text().trimmed());   // a QSO started by hand (Tx1 .. Tx5 to a new DX)
        if(txSent == 1 || txSent == 2)
            qso.rstSent = rpt(sbRpt->value());
    }
    txPeriodsMs.insert(idx * P);
    if(txPeriodsMs.size() > 500)
        txPeriodsMs.clear();
    addTxRow(e.sent, idx * P);
    qDebug().noquote() << "FT: TX" << FtUnit::modeName(curMode) << QDateTime::fromMSecsSinceEpoch(idx * P, Qt::UTC).toString("HH:mm:ss")
                       << txFreq << "Hz:" << e.sent;
    emit pttRequest(true);
    txUi();
}

void FtPanel::finishTx()
{
    if(!txOn)
        return;
    ftx.stop();
    txOn = false;
    emit pttRequest(false);
    if(txSent == 4 && chbOff73->isChecked())
    {
        pbEnable->setChecked(false);
        lbStat->setText(QString("73 sent to %1 - Enable TX off").arg(leDx->text()));
    }
    if(txSent == 3)
        rr73Sent = true;
    // RR73 or 73 sent: the QSO into the log (once)
    if((txSent == 3 || txSent == 4) && !qsoLogged && !qso.call.isEmpty() && qso.call == leDx->text().trimmed().toUpper())
        logQso();
    txUi();
}

// Halt TX: off now
void FtPanel::haltTx()
{
    if(txOn)
    {
        ftx.stop();
        txOn = false;
        emit pttRequest(false);
    }
    pbEnable->setChecked(false);
    emit txHalted();
    txUi();
}

// MOX released by hand, or TX not allowed by the main window
void FtPanel::txAborted()
{
    ftx.stop();
    txOn = false;
    if(pbEnable->isChecked())
        pbEnable->setChecked(false);
    txUi();
}

void FtPanel::addTxRow(const QString &msg, qint64 periodStartMs)
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(periodStartMs, Qt::UTC);
    const QString utc = curMode == FtUnit::WSPR ? t.toString("HHmm") : curMode == FtUnit::FT8 ? t.toString("HHmmss")
                                                                                               : t.toString("HHmmss.z").left(8);
    const int r = twAct->rowCount();
    twAct->insertRow(r);
    const QStringList cells = { utc, QString(), QString(), QString::number(txFreq) + "  ", msg, "TX", QString(), QString(), QString() };
    for(int c = 0; c < cells.size(); c++)
    {
        QTableWidgetItem *it = new QTableWidgetItem(cells[c]);
        it->setForeground(QColor(255, 230, 180));
        it->setBackground(QColor(100, 60, 0));
        if(c <= 3)
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        twAct->setItem(r, c, it);
    }
    twAct->scrollToBottom();
}

// the answer to a message to me: the Tx number (-1: none); done: the QSO is over (73 after my RR73 / 73)
int FtPanel::replyFor(const QStringList &w, bool &done) const
{
    done = false;
    const QString rest = w.size() >= 3 ? w[2] : QString();
    static const QRegularExpression rR("^R[+-]\\d\\d$"), rN("^[+-]\\d\\d$");
    if(rest.isEmpty() || isGrid4(rest))
        return 1;                       // its call (with the grid): the report
    if(rR.match(rest).hasMatch())
        return 3;                       // R+report: RR73
    if(rN.match(rest).hasMatch())
        return 2;                       // the report: R+report
    if(rest == "RRR" || rest == "RR73")
        return 4;                       // 73
    if(rest == "73")
    {
        if(txSent == 3 || txSent == 4)
            done = true;
        return 4;
    }
    return -1;
}

void FtPanel::autoSeq(qint64 periodStartMs, const QVector<FtUnit::Decode> &list, bool early)
{
    if(curMode == FtUnit::WSPR || !chbAutoSeq->isChecked())
        return;
    const QString my = leCall->text().trimmed().toUpper();
    if(my.isEmpty())
        return;
    const int P = FtUnit::periodMs(curMode);
    const int parity = (int)((periodStartMs / P) % 2);
    const bool fromDxPeriod = (parity == 0) != chbTx1st->isChecked();     // the period of the other side
    bool heard = false;
    for(const FtUnit::Decode &d : list)
    {
        const QStringList w = d.msg.split(' ', Qt::SkipEmptyParts);
        if(w.size() < 2 || strip(w[0]) != my)
            continue;
        const QString from = strip(w[1]);
        if(!isCall(from))
            continue;
        const QString dx = leDx->text().trimmed().toUpper();
        const bool cq = (bgTx->checkedId() == 5);
        if(!dx.isEmpty() && from != dx && !cq)
            continue;                   // somebody else: my QSO goes on
        const int before = bgTx->checkedId();
        bool done = false;
        const int next = replyFor(w, done);
        if(from != dx)
        {
            // an answer to my CQ: a new QSO
            leDx->setText(from);
            leDxGrid->clear();
            rr73Sent = false;
            startQso(from, QString());
        }
        else if(qso.call != from)
            startQso(from, leDxGrid->text().trimmed());
        if(w.size() >= 3 && isGrid4(w[2]))
        {
            leDxGrid->setText(w[2]);
            qso.grid = w[2];
        }
        {
            // its report of me: "-11" or "R-11"
            static const QRegularExpression rr("^R?([+-]\\d\\d)$");
            const QRegularExpressionMatch mr = rr.match(w.size() >= 3 ? w[2] : QString());
            if(mr.hasMatch())
                qso.rstRcvd = mr.captured(1);
        }
        if((next == 1 || next == 2) && (before == 0 || before == 5 || from != dx))
            sbRpt->setValue(d.snr);     // the first report of the QSO: its SNR
        genMessages();
        chbTx1st->setChecked(parity == 1);      // the other period of the DX
        rxFreq = qRound(d.freq);
        applySettings();
        wfSettings();
        heard = true;
        if(done)
        {
            pbEnable->setChecked(false);
            rr73Sent = false;
            lbStat->setText(QString("QSO with %1 complete - Enable TX off").arg(from));
        }
        else if(next >= 0)
        {
            selectTx(next);
            if(next == 3)
                rr73Sent = false;
        }
        userAction();
        break;
    }
    // my RR73 sent, the period of the DX decoded fully, no R+report again: the QSO is over
    if(!early && !heard && rr73Sent && fromDxPeriod && bgTx->checkedId() == 3)
    {
        rr73Sent = false;
        if(pbEnable->isChecked() && chbOff73->isChecked())
        {
            pbEnable->setChecked(false);
            lbStat->setText(QString("RR73 sent to %1 - Enable TX off").arg(leDx->text()));
        }
    }
}

// a double click on a decode: that station called
void FtPanel::callStation(int row)
{
    if(curMode == FtUnit::WSPR || row < 0)
        return;
    QTableWidgetItem *iu = twAct->item(row, 0), *is = twAct->item(row, 1), *iff = twAct->item(row, 3),
                     *im = twAct->item(row, 4), *ia = twAct->item(row, 5);
    if(!iu || !im || !iff || (ia && ia->text() == "TX") || !iu->data(Qt::UserRole).isValid())
        return;
    const qint64 pstart = iu->data(Qt::UserRole).toLongLong();
    const QStringList w = im->text().split(' ', Qt::SkipEmptyParts);
    if(w.size() < 2)
        return;
    const QString my = leCall->text().trimmed().toUpper();
    QString call, grid;
    int next = 0;
    if(w[0] == "CQ" || w[0] == "QRZ" || w[0] == "DE")
    {
        int i = 1;
        if(w.size() > 2 && !isCall(strip(w[1])) && isCall(strip(w[2])))
            i = 2;                      // CQ DX K1ABC FN42
        call = strip(w[i]);
        if(i + 1 < w.size() && isGrid4(w[i + 1]))
            grid = w[i + 1];
    }
    else if(!my.isEmpty() && strip(w[0]) == my)
    {
        call = strip(w[1]);
        bool done = false;
        next = replyFor(w, done);
        if(next < 0)
            next = 0;
        if(w.size() >= 3 && isGrid4(w[2]))
            grid = w[2];
    }
    else
    {
        call = strip(w[1]);
        if(w.size() >= 3 && isGrid4(w[2]))
            grid = w[2];
    }
    if(!isCall(call))
        return;
    leDx->setText(call);
    leDxGrid->setText(grid);
    if(is && !is->text().isEmpty())
        sbRpt->setValue(is->text().toInt());
    rr73Sent = false;
    if(qso.call != call || qsoLogged)
        startQso(call, grid);
    {
        static const QRegularExpression rr("^R?([+-]\\d\\d)$");
        const QRegularExpressionMatch mr = rr.match(w.size() >= 3 && strip(w[0]) == my ? w[2] : QString());
        if(mr.hasMatch())
            qso.rstRcvd = mr.captured(1);
    }
    genMessages();
    selectTx(next);
    const int P = FtUnit::periodMs(curMode);
    chbTx1st->setChecked((pstart / P) % 2 == 1);        // the other period of the station
    rxFreq = iff->text().trimmed().toInt();
    if(!chbHoldTx->isChecked())
        txFreq = rxFreq;
    applySettings();
    wfSettings();
    userAction();
    if(!pbEnable->isChecked())
        pbEnable->setChecked(true);
    txUi();
}

// ---- the log of the QSOs, QRZ.com, wsprnet.org --------------------------------------------------

void FtPanel::startQso(const QString &call, const QString &grid)
{
    qso = FtLog::Qso();
    qso.call = call.toUpper();
    qso.grid = grid;
    qso.onMs = QDateTime::currentMSecsSinceEpoch();
    qsoLogged = false;
    qsoPowerW = 0;
}

void FtPanel::notePower(double watts)
{
    if(watts > qsoPowerW)
        qsoPowerW = watts;
}

void FtPanel::logQso()
{
    qso.offMs = QDateTime::currentMSecsSinceEpoch();
    if(!leDxGrid->text().trimmed().isEmpty())
        qso.grid = leDxGrid->text().trimmed();
    qso.mode = curMode == FtUnit::FT4 ? "MFSK" : "FT8";
    qso.submode = curMode == FtUnit::FT4 ? "FT4" : QString();
    qso.myCall = leCall->text().trimmed().toUpper();
    QString g = leGrid->text().trimmed();
    if(g.size() >= 4)
        g = g.left(2).toUpper() + g.mid(2, 2) + g.mid(4).toLower();
    qso.myGrid = g;
    qso.freqHz = (dial > 0 ? dial : freqs[modeIndex(curMode)][bandOf[modeIndex(curMode)]]) + txFreq;
    qso.powerW = qsoPowerW;
    if(qso.rstSent.isEmpty())
        qso.rstSent = rpt(sbRpt->value());
    QString why;
    if(FtLog::append(qso, &why))
    {
        qsoLogged = true;
        lbStat->setText(QString("QSO with %1 logged (%2 / %3%4)").arg(qso.call, qso.rstSent, qso.rstRcvd.isEmpty() ? "?" : qso.rstRcvd)
                        .arg(qso.powerW > 0 ? QString(", %1 W").arg(qso.powerW, 0, 'f', 1) : QString()));
        qDebug().noquote() << "FT: logged" << FtLog::record(qso);
    }
    else
        lbStat->setText("The log: " + why);
    qrzUi();
}

void FtPanel::qrzUi()
{
    const int n = qrz->pending();
    pbQrz->setEnabled(!qrz->busy());
    pbQrz->setText(n > 0 ? QString("QRZ (%1)").arg(n) : QString("QRZ"));
    pbQrz->setToolTip(QString("The QSOs of the log (%1) not sent to the QRZ.com logbook yet: %2 - a click sends them\n"
                              "(the API key: Settings)").arg(FtLog::path()).arg(n));
}

// WSPR: the decodes of a period to wsprnet.org ("K1ABC FN42 37", "<PJ4/K1ABC> FK52UD 37", "PJ4/K1ABC 37")
void FtPanel::wsprSpots(qint64 periodStartMs, const QVector<FtUnit::Decode> &list)
{
    if(!chbWsprNet->isChecked() || dial <= 0)
        return;
    QList<WsprNet::Spot> spots;
    for(const FtUnit::Decode &d : list)
    {
        const QStringList w = d.msg.split(' ', Qt::SkipEmptyParts);
        if(w.size() < 2)
            continue;
        WsprNet::Spot sp;
        sp.call = strip(w[0]);
        sp.grid = (w.size() >= 3) ? w[1] : QString();
        sp.dbm = w.last().toInt();
        sp.snr = d.snr;
        sp.dt = d.dt;
        sp.drift = d.bits;
        sp.freqMHz = (dial + d.freq) / 1e6;
        if(isCall(sp.call))
            spots.append(sp);
    }
    QString g = leGrid->text().trimmed();
    if(g.size() >= 4)
        g = g.left(2).toUpper() + g.mid(2, 2) + g.mid(4).toLower();
    wnet->post(leCall->text().trimmed().toUpper(), g, dial, periodStartMs, spots,
               pbEnable->isChecked() ? sbWsprPct->value() : 0, dial + txFreq + 3, sbWsprPwr->value());
}

// the device has no transmitter (SDR-Micron): Enable TX off and disabled
void FtPanel::setTxAllowed(bool on)
{
    if(!on && (txOn || pbEnable->isChecked()))
        haltTx();
    pbEnable->setEnabled(on);
    pbHalt->setEnabled(on);
    pbEnable->setToolTip(on ? QString("Transmit in the periods of the TX parity (TX 1st), the message selected")
                            : QString("SDR-Micron is a receiver: no transmission"));
}
