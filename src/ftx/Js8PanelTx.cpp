// ANAN fork build 406: this file is MicronSDR's (N7DDC), taken as it is for the
// JS8 sub-mode of FTX in this program; the changes for it are marked "AetherSDR".
// The transmitter of the JS8 window, as JS8Call: the outgoing text, its frames, their timing, the
// automatic replies. See Js8Panel.h; the waveform: FtTx::js8Wave; the tones: js8_encode (libjs8).
//
//   - Send (or Enter in the text): the text -> the frames of the varicode of JS8Call
//     (buildMessageFrames, with my call / grid and the selected station: a text not starting with
//     a callsign is directed to it) -> one frame each period, back to back, from the start delay of
//     the period (Normal 0.5 s, Fast 0.2 s, Turbo 0.1 s, Slow 0.5 s); late by up to the dead air of
//     the period (Normal ~2.2 s): from that point. The text box shows the message as it will be
//     decoded, the frames sent marked. Halt: off now, the text cleared.
//   - CQ: "CQ CQ CQ <MYGRID4>" (Settings), the station deselected; HB: "HEARTBEAT <MYGRID4>" on a free
//     offset of 500 .. 1000 Hz (then the offset back). With an interval (Settings, min) the button
//     stays on and repeats them; a message to me stops the CQs.
//   - Reply: "<CALL> HW CPY?" (Settings), SNR: "<CALL> SNR -12" (its last SNR), the Directed menu: the
//     queries and the answers of JS8Call to the selected station.
//   - AUTO: the replies to the commands directed to my call (JS8Call processCommandActivity):
//     SNR? -> SNR, GRID? -> GRID, INFO? -> INFO, STATUS? -> STATUS, HEARING? -> up to 4 stations,
//     AGN? -> my last message again, QUERY MSGS -> NO, MSG (checksummed) -> stored (Messages, the
//     inbox file) and ACK; HB on + Heartbeat acknowledgements: a heartbeat heard -> "HEARTBEAT SNR"
//     (once in 15 min a station). AUTO off: the reply waits in the text box for Send (an ACK or a
//     heartbeat answer goes out). Not while text is typed, not while idle (TX watchdog: the minutes
//     without an action of the user, then no automatic transmissions); with the confirmation
//     (90 s, the default) unless switched off.
//   - The periods of my transmissions are not decoded.
#include "core/ForkName.h"
#include "gui/ButtonStyles.h"
#include "Js8Panel.h"
#include "FtPanel.h"   // AetherSDR: FtLatinValidator
#include "js8/decodedtext.h"
#include "js8/varicode.h"
#include <QtWidgets>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>
#include <algorithm>

static const QString EOT_TX = QString::fromUtf8(" \xE2\x99\xA2");    // the end of a message (JS8Call)

// ---- the submodes (JS8Call: JS8Submode) ------------------------------------------------------------

int Js8Panel::symbolSamples(int mode)
{
    switch(mode)
    {
        case FtUnit::JS8F: return 1200;
        case FtUnit::JS8T: return 600;
        case FtUnit::JS8S: return 3840;
        default:           return 1920;
    }
}

int Js8Panel::startDelayMs(int mode)
{
    switch(mode)
    {
        case FtUnit::JS8F: return 200;
        case FtUnit::JS8T: return 100;
        default:           return 500;     // Normal, Slow
    }
}

// the latest start in a period (ms from its start), as JS8Call: the dead air of the period
// less the TX delay (0.2 s); Fast 3/4 of it, Turbo 1/2
int Js8Panel::lateMs() const
{
    const double P = FtUnit::periodMs(curMode) / 1000.0;
    const double sig = 79.0 * symbolSamples(curMode) / 12000.0;
    double late = (P - sig) / P - 0.2 / P;
    if(curMode == FtUnit::JS8F)
        late *= 0.75;
    else if(curMode == FtUnit::JS8T)
        late *= 0.5;
    return qMax(0, (int)(late * P * 1000.0));
}

QString Js8Panel::baseCall(const QString &call)
{
    // K1ABC/P, VE/K1ABC: the longest part with a digit
    const QStringList parts = call.split('/', Qt::SkipEmptyParts);
    QString best = call;
    int bestLen = -1;
    for(const QString &p : parts)
    {
        bool digit = false;
        for(QChar c : p)
            digit |= c.isDigit();
        if(digit && p.size() > bestLen)
        {
            best = p;
            bestLen = p.size();
        }
    }
    return best;
}

QString Js8Panel::myCall() const
{
    return leCall->text().trimmed().toUpper();
}

QString Js8Panel::myGrid() const
{
    QString g = leGrid->text().trimmed().toUpper();
    return g;
}

QString Js8Panel::selectedCall() const
{
    return leTo ? leTo->text().trimmed().toUpper() : QString();
}

void Js8Panel::selectCall(const QString &call)
{
    if(!leTo)
        return;
    leTo->setText(call.trimmed().toUpper());
    txUi();
}

void Js8Panel::userAction()
{
    lastAction = QDateTime::currentMSecsSinceEpoch();
    if(idle)
    {
        idle = false;
        lbStat->setText("TX watchdog: active again");
        txUi();
    }
}

void Js8Panel::txNote(const QString &text)
{
    lbStat->setText(text);
}

// ---- the user interface ----------------------------------------------------------------------------

void Js8Panel::buildTxUi(QVBoxLayout *v)
{
    QWidget *w = new QWidget;
    QVBoxLayout *vb = new QVBoxLayout(w);
    vb->setContentsMargins(0, 2, 0, 0);
    vb->setSpacing(3);

    // the selected station, the macros, the offset, AUTO
    QHBoxLayout *h1 = new QHBoxLayout;
    h1->setSpacing(4);
    h1->addWidget(new QLabel("To"));
    leTo = new QLineEdit;
    leTo->setFixedWidth(96);
    leTo->setMaxLength(14);
    leTo->setPlaceholderText("(all)");
    leTo->setObjectName("Js8To");
    leTo->setValidator(new FtLatinValidator("[A-Za-z0-9/@]{0,14}", leTo));   // AetherSDR
    leTo->setToolTip("The selected station: the text is directed to it (a click on Stations heard puts it here;\n"
                     "@ALLCALL, @HB or a group too). Empty: the text to all");
    h1->addWidget(leTo);
    pbDesel = new QPushButton(QString::fromUtf8("\xC3\x97"));
    pbDesel->setFixedWidth(24);
    pbDesel->setToolTip("Deselect the station");
    h1->addWidget(pbDesel);
    h1->addSpacing(6);
    pbCq = new QPushButton("CQ");
    pbCq->setToolTip("CQ (Settings: the CQ message, its interval; an interval: repeated while on)");
    pbHb = new QPushButton("HB");
    pbHb->setToolTip("Heartbeat: \"HEARTBEAT <grid>\" on a free offset of 500 .. 1000 Hz (Settings: its interval;\n"
                     "an interval: repeated while on, and the heartbeats of others acknowledged with AUTO)");
    pbReply = new QPushButton("Reply");
    pbReply->setToolTip("The reply message to the selected station (Settings, \"HW CPY?\")");
    pbSnr = new QPushButton("SNR");
    pbSnr->setToolTip("The SNR of the selected station sent to it");
    tbDir = new QToolButton;
    tbDir->setText("Directed");
    tbDir->setPopupMode(QToolButton::InstantPopup);
    tbDir->setToolTip("The queries and answers of JS8Call to the selected station");
    tbDir->setStyleSheet("QToolButton::menu-indicator { image: none; }");   // AetherSDR: the theme's colours
    menuDir = new QMenu(tbDir);
    // AetherSDR: the menu in the theme's colours (no stylesheet of its own)
    tbDir->setMenu(menuDir);
    pbCq->setCheckable(true);           // on: repeated (an interval set), else one press = one
    pbHb->setCheckable(true);
    for(QPushButton *b : { pbCq, pbHb, pbReply, pbSnr })
    {
        b->setFocusPolicy(Qt::NoFocus);
        h1->addWidget(b);
    }
    pbDesel->setFocusPolicy(Qt::NoFocus);
    tbDir->setFocusPolicy(Qt::NoFocus);
    h1->addWidget(tbDir);
    h1->addStretch(1);
    h1->addWidget(new QLabel("Offset"));
    sbOff = new QSpinBox;
    sbOff->setRange(100, 4000);
    sbOff->setSuffix(" Hz");
    sbOff->setFixedWidth(92);
    sbOff->setToolTip("The offset of RX and TX (as JS8Call: one for both): the audio frequency of the lowest tone;\n"
                      "a click on a line of Band activity sets it");
    h1->addWidget(sbOff);
    pbAuto = new QPushButton("AUTO");
    pbAuto->setCheckable(true);
    pbAuto->setFocusPolicy(Qt::NoFocus);
    pbAuto->setToolTip("The automatic replies to the queries to my call (SNR?, GRID?, INFO?, STATUS?, HEARING?, AGN?,\n"
                       "MSG -> ACK) and, with HB, to the heartbeats. Off: a reply waits in the text for Send");
    AetherSDR::ButtonStyles::apply(pbAuto, "js8panel.pb.auto");
    h1->addWidget(pbAuto);
    AetherSDR::ButtonStyles::apply(pbHb, "js8panel.pb.hb");
    AetherSDR::ButtonStyles::apply(pbCq, "js8panel.pb.hb");
    vb->addLayout(h1);

    // the automatic reply waiting for the confirmation
    wConfirm = new QWidget;
    wConfirm->setAttribute(Qt::WA_StyledBackground, true);
    wConfirm->setStyleSheet("QWidget { background: #4A3A10; }");
    QHBoxLayout *hc = new QHBoxLayout(wConfirm);
    hc->setContentsMargins(4, 2, 4, 2);
    lbConfirm = new QLabel;
    hc->addWidget(lbConfirm, 1);
    pbConfYes = new QPushButton("Send");
    pbConfNo = new QPushButton("Cancel");
    for(QPushButton *b : { pbConfYes, pbConfNo })
    {
        b->setFocusPolicy(Qt::NoFocus);
        hc->addWidget(b);
    }
    wConfirm->hide();
    vb->addWidget(wConfirm);

    // the outgoing text, Send, Halt
    QHBoxLayout *h2 = new QHBoxLayout;
    h2->setSpacing(4);
    pteTx = new QPlainTextEdit;
    pteTx->setPlaceholderText("Type the outgoing message here (Enter: Send)");
    {
        QFont f("Consolas");
        f.setStyleHint(QFont::Monospace);
        f.setPointSize(10);
        pteTx->setFont(f);
        pteTx->setFixedHeight(QFontMetrics(f).lineSpacing() * 3 + 12);
    }
    pteTx->installEventFilter(this);
    h2->addWidget(pteTx, 1);
    QVBoxLayout *vs = new QVBoxLayout;
    vs->setSpacing(3);
    pbSend = new QPushButton("Send");
    pbSend->setFocusPolicy(Qt::NoFocus);
    pbSend->setMinimumWidth(70);
    // AetherSDR: this program's blue (MicronSDR: green)
    AetherSDR::ButtonStyles::apply(pbSend, "js8panel.pb.send");
    pbSend->setToolTip("Transmit the text: one frame each period from the next one (late by up to ~2 s: now)");
    pbHalt = new QPushButton("Halt");
    pbHalt->setFocusPolicy(Qt::NoFocus);
    pbHalt->setToolTip("The transmission off now, the text cleared, CQ / HB repeats off, the replies waiting dropped");
    vs->addWidget(pbSend);
    vs->addWidget(pbHalt);
    vs->addStretch(1);
    h2->addLayout(vs);
    vb->addLayout(h2);

    QHBoxLayout *h3 = new QHBoxLayout;
    lbTxState = new QLabel;
    lbTxState->setMinimumWidth(120);
    h3->addWidget(lbTxState);
    h3->addStretch(1);
    lbNext = new QLabel;
    lbNext->setStyleSheet("color: #A0C8E0;");
    h3->addWidget(lbNext);
    vb->addLayout(h3);
    v->addWidget(w);

    connect(pbDesel, &QPushButton::clicked, this, [this]() { selectCall(QString()); userAction(); });
    connect(leTo, &QLineEdit::textEdited, this, [this](const QString &t) {
        const int c = leTo->cursorPosition();
        leTo->setText(t.toUpper());
        leTo->setCursorPosition(c);
        txUi();
        userAction();
    });
    connect(sbOff, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if(sending)
        {
            sbOff->blockSignals(true);
            sbOff->setValue(rxFreq);    // not in the middle of a message
            sbOff->blockSignals(false);
            return;
        }
        rxFreq = v;
        applySettings();
        refreshBand();
        userAction();
    });
    connect(pteTx, &QPlainTextEdit::textChanged, this, [this]() {
        if(pteTx->isReadOnly())
            return;
        const QString t = pteTx->toPlainText();
        if(t != t.toUpper())
        {
            QTextCursor c = pteTx->textCursor();
            const int pos = c.position();
            pteTx->blockSignals(true);
            pteTx->setPlainText(t.toUpper());
            pteTx->blockSignals(false);
            c = pteTx->textCursor();
            c.setPosition(qMin(pos, pteTx->toPlainText().size()));
            pteTx->setTextCursor(c);
        }
        userAction();
        txUi();
    });
    connect(pbSend, &QPushButton::clicked, this, [this]() { sendText(pteTx->toPlainText(), true); });
    connect(pbHalt, &QPushButton::clicked, this, [this]() { haltTx(); userAction(); });
    connect(pbCq, &QPushButton::clicked, this, [this](bool on) {
        userAction();
        if(sbCqMin->value() <= 0)
        {
            pbCq->setChecked(false);
            sendCq();
            return;
        }
        if(on)
        {
            sendCq();
            nextCqMs = QDateTime::currentMSecsSinceEpoch() + sbCqMin->value() * 60000LL;
        }
        txUi();
    });
    connect(pbHb, &QPushButton::clicked, this, [this](bool on) {
        userAction();
        if(sbHbMin->value() <= 0)
        {
            pbHb->setChecked(false);
            sendHb();
            return;
        }
        if(on)
        {
            if(chbHbPause->isChecked())
                selectCall(QString());      // no heartbeats while in a QSO
            sendHb();
            nextHbMs = QDateTime::currentMSecsSinceEpoch() + sbHbMin->value() * 60000LL;
        }
        txUi();
    });
    connect(pbReply, &QPushButton::clicked, this, [this]() {
        userAction();
        const QString call = selectedCall();
        const QString r = macros(leReplyMsg->text().trimmed().isEmpty() ? QString("HW CPY?") : leReplyMsg->text());
        if(!call.isEmpty())
            sendText(QString("%1 %2").arg(call, r), true);
    });
    connect(pbSnr, &QPushButton::clicked, this, [this]() { userAction(); directed(" SNR", true); });
    connect(pbAuto, &QPushButton::toggled, this, [this]() { userAction(); txUi(); });
    connect(pbConfYes, &QPushButton::clicked, this, [this]() {
        if(!confQ.isEmpty())
            enqueue(confQ.first().prio, confQ.first().text, confQ.first().off, confQ.first().why);
        if(!confQ.isEmpty())
            confQ.removeFirst();
        userAction();
        showConfirm();
        processOutQ();
    });
    connect(pbConfNo, &QPushButton::clicked, this, [this]() {
        if(!confQ.isEmpty())
            confQ.removeFirst();
        userAction();
        showConfirm();
    });

    // the Directed menu (JS8Call buildQueryMenu): the queries, then the answers
    struct Item { const char *label, *cmd; bool now; const char *tip; };
    static const Item items[] = {
        { "SNR?",         " SNR?",       true,  "How do you hear me?" },
        { "GRID?",        " GRID?",      true,  "What is your grid locator?" },
        { "INFO?",        " INFO?",      true,  "What is your station information?" },
        { "STATUS?",      " STATUS?",    true,  "What is your status?" },
        { "HEARING?",     " HEARING?",   true,  "Which stations do you hear?" },
        { "QUERY MSGS",   " QUERY MSGS", true,  "Do you have messages for me?" },
        { "AGN?",         " AGN?",       true,  "Send your last message again" },
        { "QSL?",         " QSL?",       true,  "Did you copy?" },
        { "HW CPY?",      " HW CPY?",    true,  "How do you copy?" },
        { nullptr, nullptr, false, nullptr },
        { "MSG ...",      " MSG ",       false, "A message to be stored at the station (type it, then Send)" },
        { "Free text ...", " ",          false, "A free text directed to the station (type it, then Send)" },
        { nullptr, nullptr, false, nullptr },
        { "SNR (its report)", " SNR",    true,  "Its SNR as I hear it" },
        { "GRID (mine)",  " GRID",       true,  "My grid locator" },
        { "INFO (mine)",  " INFO",       true,  "My station information (Settings)" },
        { "STATUS (mine)", " STATUS",    true,  "My status (Settings)" },
        { "HEARING (mine)", " HEARING",  true,  "The stations I hear (up to 4)" },
        { nullptr, nullptr, false, nullptr },
        { "ACK",          " ACK",        true,  "Acknowledged" },
        { "NACK",         " NACK",       true,  "Not acknowledged" },
        { "QSL",          " QSL",        true,  "I copied" },
        { "YES",          " YES",        true,  "Yes" },
        { "NO",           " NO",         true,  "No" },
        { "RR",           " RR",         true,  "Roger roger" },
        { "FB",           " FB",         true,  "Fine business" },
        { "73",           " 73",         true,  "Best regards" },
        { "SK",           " SK",         true,  "End of contact" },
    };
    for(const Item &it : items)
    {
        if(!it.label)
        {
            menuDir->addSeparator();
            continue;
        }
        QAction *a = menuDir->addAction(it.label);
        a->setToolTip(it.tip);
        const QString cmd = it.cmd;
        const bool now = it.now;
        connect(a, &QAction::triggered, this, [this, cmd, now]() { userAction(); directed(cmd, now); });
    }
    menuDir->setToolTipsVisible(true);

    txTick = new QTimer(this);
    txTick->setInterval(40);
    connect(txTick, &QTimer::timeout, this, &Js8Panel::onTxTick);
    markSent();
    txUi();
}

void Js8Panel::addTxSettings(QGridLayout *g, int row)
{
    g->addWidget(new QLabel("Info:"), row, 0);
    leInfo = new QLineEdit;
    leInfo->setToolTip("My station information: the reply to INFO? (empty: no reply)");
    g->addWidget(leInfo, row, 1);
    g->addWidget(new QLabel("Status:"), row, 2);
    leStatus = new QLineEdit;
    leStatus->setToolTip("My status: the reply to STATUS? (macros: <MYIDLE> the minutes idle, <MYVERSION>)");
    g->addWidget(leStatus, row, 3);
    g->addWidget(new QLabel("CQ:"), row + 1, 0);
    leCqMsg = new QLineEdit;
    leCqMsg->setToolTip("The CQ message (macros: <MYGRID4>, <MYGRID12>, <MYCALL>)");
    g->addWidget(leCqMsg, row + 1, 1);
    g->addWidget(new QLabel("Reply:"), row + 1, 2);
    leReplyMsg = new QLineEdit;
    leReplyMsg->setToolTip("The Reply button: this to the selected station");
    g->addWidget(leReplyMsg, row + 1, 3);
    for(QLineEdit *e : { leInfo, leStatus, leCqMsg, leReplyMsg })
    {
        e->setMaxLength(120);
        connect(e, &QLineEdit::textEdited, this, [e](const QString &t) {
            const int c = e->cursorPosition();
            e->setText(t.toUpper());
            e->setCursorPosition(c);
        });
    }
    g->addWidget(new QLabel("HB every, min:"), row + 2, 0);
    sbHbMin = new QSpinBox;
    sbHbMin->setRange(0, 1440);
    sbHbMin->setToolTip("The heartbeat repeated every so many minutes while HB is on (0: HB sends one)");
    g->addWidget(sbHbMin, row + 2, 1);
    g->addWidget(new QLabel("CQ every, min:"), row + 2, 2);
    sbCqMin = new QSpinBox;
    sbCqMin->setRange(0, 1440);
    sbCqMin->setToolTip("The CQ repeated every so many minutes while CQ is on (0: CQ sends one)");
    g->addWidget(sbCqMin, row + 2, 3);
    chbHbAck = new QCheckBox("Heartbeat acknowledgements");
    chbHbAck->setToolTip("With HB on and AUTO: a heartbeat heard answered with HEARTBEAT SNR (once in 15 min a station)");
    g->addWidget(chbHbAck, row + 3, 0, 1, 2);
    chbHbPause = new QCheckBox("No heartbeats in a QSO");
    chbHbPause->setToolTip("No heartbeat acknowledgements while a station is selected; HB on deselects it");
    g->addWidget(chbHbPause, row + 3, 2, 1, 2);
    chbConfirm = new QCheckBox("Confirm the automatic replies");
    chbConfirm->setToolTip("An automatic reply waits for Send / Cancel (90 s: cancelled)");
    g->addWidget(chbConfirm, row + 4, 0, 1, 2);
    chbHoldPtt = new QCheckBox("Hold PTT between the frames");
    chbHoldPtt->setToolTip("MOX stays on between the frames of a message (off: off in the gaps, as JS8Call)");
    g->addWidget(chbHoldPtt, row + 4, 2, 1, 2);
    g->addWidget(new QLabel("TX watchdog, min:"), row + 5, 0);
    sbWatchdog = new QSpinBox;
    sbWatchdog->setRange(0, 1440);
    sbWatchdog->setToolTip("So many minutes without an action of the user: idle - no automatic transmissions\n"
                           "(replies, heartbeats, CQ repeats) until the next action (0: never)");
    g->addWidget(sbWatchdog, row + 5, 1);
}

void Js8Panel::loadTxSettings(QSettings &st)
{
    leInfo->setText(st.value("MyInfo", "").toString());
    leStatus->setText(st.value("MyStatus", "IDLE <MYIDLE> VERSION <MYVERSION>").toString());
    leCqMsg->setText(st.value("CQMessage", "CQ CQ CQ <MYGRID4>").toString());
    leReplyMsg->setText(st.value("ReplyMessage", "HW CPY?").toString());
    sbHbMin->setValue(st.value("HBIntervalMin", 0).toInt());
    sbCqMin->setValue(st.value("CQIntervalMin", 0).toInt());
    chbHbAck->setChecked(st.value("HBAck", true).toBool());
    chbHbPause->setChecked(st.value("HBPauseQso", true).toBool());
    chbConfirm->setChecked(st.value("AutoreplyConfirm", true).toBool());
    chbHoldPtt->setChecked(st.value("HoldPtt", false).toBool());
    sbWatchdog->setValue(st.value("TxWatchdogMin", 60).toInt());
    pbAuto->setChecked(st.value("Auto", false).toBool());
    sbOff->blockSignals(true);
    sbOff->setValue(rxFreq);
    sbOff->blockSignals(false);
    txUi();
}

void Js8Panel::saveTxSettings(QSettings &st)
{
    st.setValue("MyInfo", leInfo->text().trimmed());
    st.setValue("MyStatus", leStatus->text().trimmed());
    st.setValue("CQMessage", leCqMsg->text().trimmed());
    st.setValue("ReplyMessage", leReplyMsg->text().trimmed());
    st.setValue("HBIntervalMin", sbHbMin->value());
    st.setValue("CQIntervalMin", sbCqMin->value());
    st.setValue("HBAck", chbHbAck->isChecked());
    st.setValue("HBPauseQso", chbHbPause->isChecked());
    st.setValue("AutoreplyConfirm", chbConfirm->isChecked());
    st.setValue("HoldPtt", chbHoldPtt->isChecked());
    st.setValue("TxWatchdogMin", sbWatchdog->value());
    st.setValue("Auto", pbAuto->isChecked());
}

void Js8Panel::txUi()
{
    if(!pbSend)
        return;
    const bool sel = !selectedCall().isEmpty();
    pbSend->setEnabled(txAllowed && !sending);
    for(QWidget *x : std::initializer_list<QWidget *>{ pbCq, pbHb, pbAuto })
        x->setEnabled(txAllowed);
    pbReply->setEnabled(txAllowed && sel);
    pbSnr->setEnabled(txAllowed && sel && stations.contains(selectedCall()));
    tbDir->setEnabled(txAllowed && sel);
    pbHalt->setEnabled(txAllowed);
    if(sbOff->value() != rxFreq)
    {
        sbOff->blockSignals(true);
        sbOff->setValue(rxFreq);
        sbOff->blockSignals(false);
    }
    sbOff->setEnabled(!sending);
    if(!txAllowed)
    {
        lbTxState->setText("RX only (SDR-Micron)");
        lbTxState->setStyleSheet("color: #A0A0A0;");
    }
    else if(txOn)
    {
        lbTxState->setText(QString("TX %1/%2 at %3 Hz").arg(txFramesSent).arg(txFramesTotal).arg(rxFreq));
        lbTxState->setStyleSheet("color: white; background: #A02020; padding: 1px 4px;");
    }
    else if(sending)
    {
        lbTxState->setText(QString("TX %1/%2: the next period").arg(txFramesSent + 1).arg(txFramesTotal));
        lbTxState->setStyleSheet("color: #FF9090;");
    }
    else if(idle)
    {
        lbTxState->setText("Idle timeout");
        lbTxState->setStyleSheet("color: white; background: black; padding: 1px 4px;");
    }
    else
    {
        lbTxState->setText(pbAuto->isChecked() ? "Receiving (AUTO)" : "Receiving");
        lbTxState->setStyleSheet("color: #90E090;");
    }
    // the repeats
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList nx;
    auto left = [now](qint64 t) {
        const qint64 s = qMax<qint64>(0, (t - now + 999) / 1000);
        return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
    };
    if(pbHb->isChecked() && sbHbMin->value() > 0)
        nx << "HB in " + left(nextHbMs);
    if(pbCq->isChecked() && sbCqMin->value() > 0)
        nx << "CQ in " + left(nextCqMs);
    if(!outQ.isEmpty())
        nx << QString("%1 waiting").arg(outQ.size());
    lbNext->setText(nx.join("   "));
}

// SDR-Micron: no transmitter
void Js8Panel::setTxAllowed(bool on)
{
    if(!on)
    {
        haltTx();
        if(pbAuto)
            pbAuto->setChecked(false);
    }
    txAllowed = on;
    txUi();
}

bool Js8Panel::eventFilter(QObject *o, QEvent *e)
{
    if(o == pteTx && e->type() == QEvent::KeyPress)
    {
        QKeyEvent *k = static_cast<QKeyEvent *>(e);
        if((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && !(k->modifiers() & Qt::ShiftModifier))
        {
            if(!pteTx->isReadOnly())
                sendText(pteTx->toPlainText(), true);
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

// ---- the messages ----------------------------------------------------------------------------------

// the macros of JS8Call
QString Js8Panel::macros(const QString &text) const
{
    QString t = text;
    const QString g = myGrid();
    const qint64 idleMin = qMax<qint64>(0, (QDateTime::currentMSecsSinceEpoch() - lastAction) / 60000);
    t.replace("<MYCALL>", myCall());
    t.replace("<MYGRID4>", g.left(4));
    t.replace("<MYGRID12>", g);
    t.replace("<MYIDLE>", QString("%1M").arg(idleMin));
    t.replace("<MYVERSION>", AetherSDR::forkDisplayVersion().toUpper());   // AetherSDR, build 408: goes on the air
    t.replace("<MYINFO>", leInfo->text().trimmed().toUpper());
    t.replace("<MYSTATUS>", leStatus->text().trimmed().toUpper());
    const QString call = selectedCall();
    t.replace("<CALL>", call);
    if(t.contains("<SNR>"))
        t.replace("<SNR>", stations.contains(call) ? Varicode::formatSNR(stations.value(call).lastSnr) : QString());
    return t;
}

// the characters JS8 can carry: upper case, one line, the others out
QString Js8Panel::cleanText(const QString &text)
{
    static QSet<QString> valid;
    if(valid.isEmpty())
    {
        valid = Varicode::huffValidChars(Varicode::defaultHuffTable());
        valid << "@" << ":";
    }
    QString t = text.toUpper();
    t.replace(QRegularExpression("[\\r\\n\\t]+"), " ");
    QString out;
    for(QChar c : t)
        if(valid.contains(QString(c)))
            out += c;
    return out.simplified();
}

// the text into frames, sent from the next period (fromUser: an action of the user; useSelected:
// directed to the selected station - the automatic messages begin with their callsign)
bool Js8Panel::sendText(const QString &textIn, bool fromUser, bool useSelected)
{
    if(fromUser)
        userAction();
    if(!txAllowed)
    {
        lbStat->setText("SDR-Micron is a receiver: no transmission");
        return false;
    }
    if(!running)
    {
        lbStat->setText("TX: JS8 is not running");
        return false;
    }
    if(!unit->hasJs8Encoder())
    {
        lbStat->setText("TX: libjs8.dll has no encoder (js8_encode) - build JS8_src again");
        return false;
    }
    if(myCall().isEmpty() || myGrid().size() < 4)
    {
        lbStat->setText("TX: my call and locator are needed (Settings)");
        pbSettings->setChecked(true);
        return false;
    }
    if(sending)
    {
        lbStat->setText("TX: a message is being sent (Halt first)");
        return false;
    }
    const QString text = cleanText(macros(textIn));
    if(text.isEmpty())
        return false;
    // JS8Call: a key stuck (the same character again and again) is not sent
    if(text.size() > 5 && QString(text).remove(text.at(0)).trimmed().isEmpty())
    {
        lbStat->setText("TX: the same character again and again - not sent");
        return false;
    }
    const int sub = submodeOf(curMode);
    const QList<QPair<QString, int>> frames = Varicode::buildMessageFrames(myCall(), myGrid().left(4),
                                                     useSelected ? selectedCall() : QString(), text,
                                                     true, false, sub);
    if(frames.isEmpty())
    {
        lbStat->setText(QString("TX: \"%1\" can not be sent").arg(text));
        return false;
    }
    txQueue.clear();
    QStringList all;
    for(const auto &f : frames)
    {
        txQueue.enqueue(TxFrame { f.first, f.second });
        all << DecodedText(f.first, f.second, sub).message();
    }
    txAllText = all.join("");
    txFramesTotal = frames.size();
    txFramesSent = 0;
    txSentText.clear();
    sending = true;
    lastTxIdx = -1;
    pteTx->blockSignals(true);
    pteTx->setPlainText(txAllText.trimmed());
    pteTx->blockSignals(false);
    pteTx->setReadOnly(true);
    markSent();
    qDebug().noquote() << "JS8: TX" << FtUnit::modeName(curMode) << rxFreq << "Hz," << frames.size() << "frames:" << txAllText;
    txUi();
    return true;
}

// the text box: the part sent marked
void Js8Panel::markSent()
{
    QList<QTextEdit::ExtraSelection> sel;
    const int n = qMin(txSentText.size(), pteTx->toPlainText().size());
    if(sending && n > 0)
    {
        QTextEdit::ExtraSelection x;
        x.cursor = QTextCursor(pteTx->document());
        x.cursor.setPosition(0);
        x.cursor.setPosition(n, QTextCursor::KeepAnchor);
        x.format.setBackground(QColor(110, 60, 20));
        x.format.setForeground(QColor(255, 220, 180));
        sel << x;
    }
    pteTx->setExtraSelections(sel);
    // sending: the box brown and read only (the style sheet of the window sets the colours)
    pteTx->setStyleSheet(sending ? "QPlainTextEdit { background: #2A1C10; color: #FFD8B0; border: 1px solid #A06030; }"
                                 : "QPlainTextEdit { background: #202020; color: #FFFFFF; border: 1px solid #707070; }");
}

// Messages: my transmission, frame by frame (the line of the message)
void Js8Panel::addTxLine(const QString &text, qint64 periodStartMs)
{
    int li = -1;
    if(txFramesSent > 1)
        for(int i = lines.size() - 1; i >= 0 && i >= lines.size() - 50; i--)
            if(lines[i].tx && !lines[i].done)
            {
                li = i;
                break;
            }
    if(li < 0)
    {
        Line l;
        l.t = QDateTime::fromMSecsSinceEpoch(periodStartMs, Qt::UTC);
        l.off = rxFreq;
        l.snr = 0;
        l.done = false;
        l.hb = false;
        l.tx = true;
        lines.append(l);
        li = lines.size() - 1;
    }
    lines[li].text += text;
    if(txQueue.isEmpty())
    {
        lines[li].text = Varicode::rstrip(lines[li].text) + EOT_TX;
        lines[li].done = true;
    }
    while(lines.size() > 400)
        lines.removeFirst();
    refreshMessages();
}

// ---- the timing ------------------------------------------------------------------------------------

void Js8Panel::onTxTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if(txOn)
    {
        if(jtx.done() || now > jtx.startMs() + jtx.lengthMs() + 1500)
            finishFrame();
        else
            return;
    }
    // once a second: the watchdog, the repeats, the confirmation, the replies waiting
    if(now / 1000 != lastSecond)
    {
        lastSecond = now / 1000;
        if(sbWatchdog->value() > 0 && !idle && now - lastAction > sbWatchdog->value() * 60000LL)
        {
            idle = true;
            pbHb->setChecked(false);
            pbCq->setChecked(false);
            outQ.clear();
            confQ.clear();
            showConfirm();
            lbStat->setText(QString("TX watchdog: %1 min without an action - no automatic transmissions").arg(sbWatchdog->value()));
        }
        if(!idle && txAllowed)
        {
            if(pbHb->isChecked() && sbHbMin->value() > 0 && now >= nextHbMs)
            {
                sendHb();
                nextHbMs = now + sbHbMin->value() * 60000LL;
            }
            if(pbCq->isChecked() && sbCqMin->value() > 0 && now >= nextCqMs && !sending)
            {
                sendCq();
                nextCqMs = now + sbCqMin->value() * 60000LL;
            }
        }
        if(!confQ.isEmpty() && now > confirmUntil)
        {
            lbStat->setText(QString("The automatic reply \"%1\" not confirmed in 90 s - cancelled").arg(confQ.first().text));
            confQ.removeFirst();
            showConfirm();
        }
        else if(!confQ.isEmpty())
            lbConfirm->setText(QString("Automatic reply: <b>%1</b> (to %2) - %3 s")
                               .arg(confQ.first().text.toHtmlEscaped(), confQ.first().why.toHtmlEscaped())
                               .arg(qMax<qint64>(0, (confirmUntil - now) / 1000)));
        processOutQ();
        txUi();
    }
    if(!sending || txOn)
        return;
    if(txQueue.isEmpty())
    {
        endMessage(true);
        return;
    }
    const int P = FtUnit::periodMs(curMode);
    const qint64 idx = now / P;
    const qint64 off = now - idx * P;
    if(idx == lastTxIdx)
        return;                         // one frame a period
    if(off >= lateMs())
        return;                         // too late for this one: the next
    startFrame(idx);
}

void Js8Panel::startFrame(qint64 idx)
{
    const int P = FtUnit::periodMs(curMode);
    const int sub = submodeOf(curMode);
    TxFrame f = txQueue.dequeue();
    int bits = f.bits;
    if(txFramesSent > 0)
        bits &= ~Varicode::JS8CallFirst;    // (JS8Call: prepareNextMessageFrame)
    if(txQueue.isEmpty())
        bits |= Varicode::JS8CallLast;
    int tones[79];
    if(!unit->js8Encode(sub, bits, f.frame, tones))
    {
        lbStat->setText(QString("TX: the frame [%1] can not be encoded - the message stopped").arg(f.frame));
        endMessage(false);
        return;
    }
    std::vector<float> w = FtTx::js8Wave(tones, rxFreq, symbolSamples(curMode));
    jtx.arm(std::move(w), idx * P + startDelayMs(curMode));
    if(!jtx.active())
        jtx.start();
    txOn = true;
    txIdx = lastTxIdx = idx;
    lastTxStartMs = QDateTime::currentMSecsSinceEpoch();
    txFramesSent++;
    const QString msg = DecodedText(f.frame, bits, sub).message();
    txSentText += msg;
    txPeriodsMs.insert(idx * P);
    if(txPeriodsMs.size() > 500)
        txPeriodsMs.clear();
    addTxLine(msg, idx * P);
    markSent();
    qDebug().noquote() << "JS8: TX frame" << txFramesSent << "/" << txFramesTotal
                       << QDateTime::fromMSecsSinceEpoch(idx * P, Qt::UTC).toString("HH:mm:ss") << rxFreq << "Hz"
                       << "[" + f.frame + "]" << bits << msg;
    if(!pttHeld)
        emit pttRequest(true);
    pttHeld = false;
    txUi();
}

void Js8Panel::finishFrame()
{
    if(!txOn)
        return;
    txOn = false;
    const bool more = sending && !txQueue.isEmpty();
    if(more && chbHoldPtt->isChecked())
        pttHeld = true;                 // MOX on, silence until the next frame
    else
    {
        jtx.stop();
        emit pttRequest(false);
    }
    if(!more)
        endMessage(true);
    txUi();
}

// the message over (completed: all its frames sent - the text box empty again)
void Js8Panel::endMessage(bool completed)
{
    if(pttHeld)
    {
        pttHeld = false;
        jtx.stop();
        emit pttRequest(false);
    }
    const bool was = sending;
    sending = false;
    txQueue.clear();
    if(was)
    {
        if(!txSentText.isEmpty())
            lastTxMessage = txSentText;
        pteTx->setReadOnly(false);
        if(completed)
        {
            pteTx->blockSignals(true);
            pteTx->clear();
            pteTx->blockSignals(false);
        }
        // a message stopped in the middle: its line in Messages ends here
        for(int i = lines.size() - 1; i >= 0 && i >= lines.size() - 50; i--)
            if(lines[i].tx && !lines[i].done)
            {
                lines[i].done = true;
                if(!completed)
                    lines[i].text += " ...";
                break;
            }
        refreshMessages();
    }
    if(restoreOff > 0)
    {
        rxFreq = restoreOff;            // the offset before the heartbeat
        restoreOff = -1;
        applySettings();
    }
    markSent();
    txUi();
}

// Halt: off now, the text cleared (as JS8Call), the repeats off, the replies waiting dropped
void Js8Panel::haltTx()
{
    if(txOn || pttHeld)
    {
        jtx.stop();
        txOn = false;
        pttHeld = false;
        emit pttRequest(false);
    }
    endMessage(false);
    if(pteTx)
    {
        pteTx->blockSignals(true);
        pteTx->clear();
        pteTx->blockSignals(false);
    }
    if(pbHb)
    {
        pbHb->setChecked(false);
        pbCq->setChecked(false);
    }
    outQ.clear();
    confQ.clear();
    if(wConfirm)
        showConfirm();
    txUi();
}

// MOX released by hand, or TX refused by the main window
void Js8Panel::txAborted()
{
    jtx.stop();
    txOn = false;
    pttHeld = false;
    endMessage(false);
    txUi();
}

// ---- the queue of the automatic messages (JS8Call: m_txMessageQueue, processTxQueue) --------------

void Js8Panel::enqueue(int prio, const QString &text, int off, const QString &why)
{
    Outgoing o { prio, text, off, QDateTime::currentMSecsSinceEpoch(), why };
    // by priority, the same ones in their order
    int i = 0;
    while(i < outQ.size() && outQ[i].prio >= prio)
        i++;
    outQ.insert(i, o);
    txUi();
}

void Js8Panel::processOutQ()
{
    if(outQ.isEmpty() || sending || txOn || !running)
        return;
    if(idle || !txAllowed)
    {
        outQ.clear();
        return;
    }
    if(!pteTx->toPlainText().trimmed().isEmpty())
        return;                         // the user is typing
    const Outgoing &h = outQ.first();
    // a low priority message: not within 30 s of my last transmission
    if(h.prio <= PRIO_LOW && QDateTime::currentMSecsSinceEpoch() - lastTxStartMs <= 30000)
        return;
    const Outgoing o = outQ.takeFirst();
    // sent at once: AUTO on, or a heartbeat / its answer / an ACK (as JS8Call); else in the text
    // box for Send
    const bool now = pbAuto->isChecked() || o.prio >= PRIO_HIGH || o.text.contains(" HEARTBEAT") ||
                     o.text.contains(" HB ") || o.text.contains(" ACK");
    if(!now)
    {
        pteTx->setPlainText(o.text);
        lbStat->setText(QString("The reply to %1 is in the text: Send").arg(o.why));
        return;
    }
    const int old = rxFreq;
    if(o.off > 0 && o.off != rxFreq)
    {
        rxFreq = o.off;
        applySettings();
    }
    if(sendText(o.text, false, false))
    {
        if(rxFreq != old)
            restoreOff = old;           // back after it
    }
    else if(rxFreq != old)
    {
        rxFreq = old;
        applySettings();
    }
}

void Js8Panel::askConfirm(const Outgoing &o)
{
    confQ.append(o);
    if(confQ.size() == 1)
    {
        confirmUntil = QDateTime::currentMSecsSinceEpoch() + 90000;
        QApplication::alert(this);
    }
    showConfirm();
}

void Js8Panel::showConfirm()
{
    if(confQ.isEmpty())
    {
        wConfirm->hide();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if(confirmUntil < now)
        confirmUntil = now + 90000;     // the next one: its own 90 s
    lbConfirm->setText(QString("Automatic reply: <b>%1</b> (to %2) - %3 s")
                       .arg(confQ.first().text.toHtmlEscaped(), confQ.first().why.toHtmlEscaped())
                       .arg((confirmUntil - now) / 1000));
    wConfirm->show();
}

// ---- CQ, HB, the directed messages -----------------------------------------------------------------

void Js8Panel::sendCq()
{
    QString m = leCqMsg->text().trimmed();
    if(m.isEmpty())
        m = "CQ CQ CQ <MYGRID4>";
    selectCall(QString());              // (JS8Call: the station deselected)
    if(sending)
        return;
    sendText(m, false, false);
}

void Js8Panel::sendHb()
{
    // JS8Call sendHeartbeat: "MYCALL: HEARTBEAT GRID4" on a free offset of 500 .. 1000 Hz (the
    // offset now when it is in there or below)
    const QString m = QString("%1: HEARTBEAT %2").arg(myCall(), myGrid().left(4)).trimmed();
    const int f = rxFreq <= 1000 ? -1 : freeOffset(500, 1000, 50);
    enqueue(PRIO_LOW + 1, m, f, "HB");
    processOutQ();
}

// a command of the Directed menu (and SNR) to the selected station; now: sent at once, else in the
// text box to be completed
void Js8Panel::directed(const QString &cmd, bool now)
{
    const QString call = selectedCall();
    if(call.isEmpty())
    {
        lbStat->setText("Select a station first (Stations heard, or type its call into To)");
        return;
    }
    QString text = call + cmd;
    if(cmd == " SNR")
    {
        if(!stations.contains(call))
        {
            lbStat->setText(QString("%1: no SNR (not heard)").arg(call));
            return;
        }
        text += " " + Varicode::formatSNR(stations.value(call).lastSnr);
    }
    else if(cmd == " GRID")
        text += " " + myGrid();
    else if(cmd == " INFO")
    {
        if(leInfo->text().trimmed().isEmpty())
        {
            lbStat->setText("My INFO is empty (Settings)");
            return;
        }
        text += " " + macros(leInfo->text().trimmed());
    }
    else if(cmd == " STATUS")
        text += " " + macros(leStatus->text().trimmed());
    else if(cmd == " HEARING")
    {
        QList<Station> all = stations.values();
        std::sort(all.begin(), all.end(), [](const Station &a, const Station &b) { return a.last > b.last; });
        QStringList calls;
        for(const Station &s : all)
            if(s.call != call && calls.size() < 4)
                calls << s.call;
        text += " " + calls.join(' ');
    }
    if(!now)
    {
        if(sending)
            return;
        pteTx->setPlainText(text);
        QTextCursor c = pteTx->textCursor();
        c.movePosition(QTextCursor::End);
        pteTx->setTextCursor(c);
        pteTx->setFocus();
        return;
    }
    sendText(text.trimmed(), true, false);
}

// ---- the free offsets (JS8Call isFreqOffsetFree / findFreeFreqOffset) -----------------------------

bool Js8Panel::isOffFree(int f, int bw) const
{
    if(f == rxFreq)
        return true;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for(auto it = acts.constBegin(); it != acts.constEnd(); ++it)
    {
        if(it.value().last.secsTo(now) >= 30)
            continue;
        if(qAbs(it.key() - f) < bw)
            return false;
    }
    return true;
}

int Js8Panel::freeOffset(int fmin, int fmax, int bw) const
{
    const int nslots = qMax(1, (fmax - fmin) / bw);
    for(int i = 0; i < nslots; i++)
    {
        const int f = fmin + bw * (int)QRandomGenerator::global()->bounded(nslots);
        if(isOffFree(f, bw))
            return f;
    }
    for(int i = 0; i < nslots; i++)
    {
        const int f = fmin + (int)QRandomGenerator::global()->bounded(fmax - fmin);
        if(isOffFree(f, bw))
            return f;
    }
    return fmin;
}

// ---- the commands heard (JS8Call: processDecodeEvent, the buffers, processCommandActivity) ---------

void Js8Panel::heard(const FtUnit::Decode &d, const DecodedText &dt, qint64 tMs)
{
    const int off = (int)lround(d.freq);
    const int thr = curMode == FtUnit::JS8F ? 16 : curMode == FtUnit::JS8T ? 32 : 10;
    const bool first = (d.bits & Varicode::JS8CallFirst) == Varicode::JS8CallFirst;
    const bool last = (d.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast;
    // the buffers older than 90 s out
    for(auto it = bufs.begin(); it != bufs.end(); )
        it = (tMs - it.value().tMs > 90000) ? bufs.erase(it) : it + 1;
    auto bufNear = [this, off, thr]() {
        for(auto it = bufs.begin(); it != bufs.end(); ++it)
            if(qAbs(it.key() - off) <= thr)
                return it.key();
        return -1;
    };
    if(first)
    {
        const int k = bufNear();
        if(k >= 0)
            bufs.remove(k);             // a new message there
    }
    Cmd c;
    c.snr = d.snr;
    c.off = off;
    c.bits = d.bits;
    c.tMs = tMs;
    if(dt.isHeartbeat())
    {
        c.from = dt.compoundCall().trimmed();
        c.to = dt.isAlt() ? "@ALLCALL" : "@HB";
        c.cmd = dt.isAlt() ? " CQ" : " HEARTBEAT";
        c.grid = dt.extra().trimmed();
        command(c);
        return;
    }
    if(dt.isDirectedMessage())
    {
        const QStringList parts = dt.directedMessage();
        c.from = parts.value(0);
        c.to = parts.value(1);
        c.cmd = parts.value(2);
        c.extra = parts.mid(3).join(" ");
        if(c.from == "<....>" || c.to == "<....>")
            return;                     // compound callsigns: not answered
        if(Varicode::isCommandBuffered(c.cmd) && !last)
        {
            bufs.insert(off, c);        // its text in the next frames
            return;
        }
        command(c);
        return;
    }
    if(dt.isCompound())
        return;
    // a data frame: the text of a buffered command
    const int k = bufNear();
    if(k < 0)
        return;
    Cmd b = bufs.take(k);
    b.text += dt.message();
    b.tMs = tMs;
    if(!last)
    {
        bufs.insert(off, b);
        return;
    }
    QString msg = Varicode::rstrip(b.text);
    bool valid = true;
    const int cs = Varicode::isCommandChecksumed(b.cmd);
    if(cs == 32)
    {
        msg = Varicode::lstrip(msg);
        const QString sum = msg.right(6);
        msg = msg.left(msg.size() - 7);
        valid = Varicode::checksum32Valid(sum, msg);
    }
    else if(cs == 16)
    {
        msg = Varicode::lstrip(msg);
        const QString sum = msg.right(3);
        msg = msg.left(msg.size() - 4);
        valid = Varicode::checksum16Valid(sum, msg);
    }
    if(!valid)
    {
        qDebug().noquote() << "JS8: buffered" << b.cmd << "from" << b.from << "failed its checksum:" << b.text;
        return;
    }
    b.text = msg;
    b.bits |= Varicode::JS8CallLast;
    command(b);
}

void Js8Panel::command(const Cmd &c)
{
    const QString my = myCall();
    if(my.isEmpty() || c.from.isEmpty() || c.from == my || baseCall(c.from) == baseCall(my))
        return;
    const bool toMe = c.to == my || c.to == baseCall(my);
    const bool hb = (c.cmd == " HB" || c.cmd == " HEARTBEAT") && (c.to == "@HB" || c.to == "@ALLCALL");
    qDebug().noquote() << "JS8: command" << c.from << "->" << c.to << "[" + c.cmd + "]" << c.extra << c.text
                       << "snr" << c.snr << "off" << c.off << (toMe ? "(to me)" : "");
    if(toMe)
    {
        // JS8Call: a message to me stops the CQ repeats, the heartbeat interval starts again
        if(pbCq->isChecked())
            pbCq->setChecked(false);
        if(pbHb->isChecked() && sbHbMin->value() > 0)
            nextHbMs = QDateTime::currentMSecsSinceEpoch() + sbHbMin->value() * 60000LL;
        QApplication::alert(this);
        lbStat->setText(QString("To me from %1:%2 %3").arg(c.from, c.cmd, (c.extra + " " + c.text).trimmed()));
    }
    if(!toMe && !hb)
        return;
    if(idle || !txAllowed || !running)
        return;
    QString reply;
    int prio = PRIO_NORMAL, off = -1;
    if(toMe)
    {
        if(c.cmd == " SNR?" || c.cmd == "?")
            reply = QString("%1 SNR %2").arg(c.from, Varicode::formatSNR(c.snr));
        else if(c.cmd == " INFO?")
        {
            if(!leInfo->text().trimmed().isEmpty())
                reply = QString("%1 INFO %2").arg(c.from, macros(leInfo->text().trimmed()));
        }
        else if(c.cmd == " STATUS?")
        {
            if(!leStatus->text().trimmed().isEmpty())
                reply = QString("%1 STATUS %2").arg(c.from, macros(leStatus->text().trimmed()));
        }
        else if(c.cmd == " GRID?")
        {
            if(!myGrid().isEmpty())
                reply = QString("%1 GRID %2").arg(c.from, myGrid());
        }
        else if(c.cmd == " HEARING?")
        {
            QList<Station> all = stations.values();
            std::sort(all.begin(), all.end(), [](const Station &a, const Station &b) { return a.last > b.last; });
            QStringList calls;
            calls << QString("%1 HEARING").arg(c.from);
            int n = 0;
            for(const Station &s : all)
                if(s.call != c.from && n < 4)
                {
                    calls << s.call;
                    n++;
                }
            reply = calls.join(' ');
        }
        else if(c.cmd == " AGN?")
        {
            if(!lastTxMessage.isEmpty())
                reply = Varicode::rstrip(lastTxMessage).trimmed();
        }
        else if(c.cmd == " QUERY MSGS" || c.cmd == " QUERY MSGS?")
            reply = QString("%1 NO").arg(c.from);          // no messages stored here
        else if(c.cmd == " MSG")
        {
            storeMsg(c);
            reply = QString("%1 ACK").arg(c.from);
        }
    }
    else if(hb && c.cmd != " CQ" && pbHb->isChecked() && pbAuto->isChecked() && chbHbAck->isChecked())
    {
        // the heartbeat acknowledgement: not in a QSO, once in 15 min a station
        if(chbHbPause->isChecked() && !selectedCall().isEmpty())
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if(hbAcked.contains(c.from) && now - hbAcked.value(c.from) < 15 * 60000LL)
            return;
        hbAcked.insert(c.from, now);
        reply = QString("%1 HEARTBEAT SNR %2").arg(c.from, Varicode::formatSNR(c.snr));
        prio = PRIO_LOW + 1;
        off = freeOffset(500, 1000, 50);
    }
    if(reply.isEmpty())
        return;
    if(!pteTx->toPlainText().trimmed().isEmpty() && !sending)
        return;                         // JS8Call: no reply while text is in the box
    const QString why = QString("%1%2").arg(c.from, c.cmd);
    qDebug().noquote() << "JS8: reply" << reply << "(" + why + ")";
    if(chbConfirm->isChecked())
        askConfirm(Outgoing { prio, reply, off, QDateTime::currentMSecsSinceEpoch(), why });
    else
    {
        enqueue(prio, reply, off, why);
        processOutQ();
    }
}

// a MSG to me: into Messages (it is there already, yellow) and the inbox file
void Js8Panel::storeMsg(const Cmd &c)
{
    const QString dir = QDir::homePath() + "/AetherSDR-FTX";
    QDir().mkpath(dir);
    QFile f(dir + "/js8_inbox.txt");
    if(f.open(QIODevice::Append | QIODevice::Text))
    {
        const QString line = QString("%1  %2 -> %3: %4\n")
            .arg(QDateTime::fromMSecsSinceEpoch(c.tMs, Qt::UTC).toString("yyyy-MM-dd HH:mm:ss"), c.from, c.to, c.text);
        f.write(line.toUtf8());
    }
    lbStat->setText(QString("A message from %1 (%2): %3").arg(c.from, QDir::toNativeSeparators(dir + "/js8_inbox.txt"), c.text));
    QApplication::alert(this);
}
