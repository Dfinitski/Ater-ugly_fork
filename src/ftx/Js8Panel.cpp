// ANAN fork build 406: this file is MicronSDR's (N7DDC), taken as it is for the
// JS8 sub-mode of FTX in this program; the changes for it are marked "AetherSDR".
// JS8 receiver window: see Js8Panel.h
#include "Js8Panel.h"
#include "PskReporter.h"
#include "CtyDat.h"
#include "FtPanel.h"   // AetherSDR: FtLatinValidator
#include "core/ForkName.h"
#include "js8/decodedtext.h"
#include "js8/varicode.h"
#include <QtWidgets>
#include <QSettings>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>
#include <algorithm>

const char *Js8Panel::bandName[Js8Panel::BANDS] = { "160m", "80m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m" };
// JS8Call: its frequency list (all the submodes on the same dial frequencies)
const qint64 Js8Panel::defFreq[Js8Panel::BANDS] = {
    1842000, 3578000, 7078000, 10130000, 14078000, 18104000, 21078000, 24922000, 28078000, 50318000 };

static const QString EOT_MARK = QString::fromUtf8(" \xE2\x99\xA2");    // the end of a message (JS8Call)

Js8Panel::Js8Panel(FtUnit *u, QWidget *parent)
    : QWidget(parent), unit(u), curMode(FtUnit::JS8N), dial(0), running(false), rxFreq(1500),
      autoSaved(0), band(2)
{
    setObjectName("Js8Panel");
    setWindowFlags(Qt::Window);         // a window of its own
    setWindowTitle("AetherSDR JS8");   // AetherSDR
    setMinimumSize(726, 420);   // AetherSDR: as the FTX window
    setAttribute(Qt::WA_StyledBackground, true);
    // AetherSDR: no stylesheet of its own -- the window takes the program's theme
    psk = new PskReporter(AetherSDR::forkDisplayVersion(), this);   // AetherSDR
    for(int b = 0; b < BANDS; b++)
        freqs[b] = defFreq[b];

    QVBoxLayout *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 4, 6, 4);
    v->setSpacing(4);

    // the submode, the band, the dial frequency
    QHBoxLayout *h1 = new QHBoxLayout;
    // AetherSDR, build 406: back to the FT8 / FT4 / WSPR window (MicronSDR: its FTX button)
    {
        QPushButton *bFtx = new QPushButton("FTX");
        bFtx->setFocusPolicy(Qt::NoFocus);
        bFtx->setToolTip("FT8 / FT4 / WSPR: the FTX window instead of this one");
        connect(bFtx, &QPushButton::clicked, this, [this]() { emit ftxRequested(); });
        h1->addWidget(bFtx);
        h1->addSpacing(6);
    }
    bgMode = new QButtonGroup(this);
    const int modes[4] = { FtUnit::JS8N, FtUnit::JS8F, FtUnit::JS8T, FtUnit::JS8S };
    const char *names[4] = { "Normal", "Fast", "Turbo", "Slow" };
    const char *tips[4] = { "JS8 Normal: 15 s periods, 50 Hz wide", "JS8 Fast: 10 s periods, 80 Hz wide",
                            "JS8 Turbo: 6 s periods, 160 Hz wide", "JS8 Slow: 30 s periods, 25 Hz wide" };
    for(int i = 0; i < 4; i++)
    {
        QPushButton *b = new QPushButton(names[i]);
        b->setCheckable(true);
        b->setMinimumWidth(i == 0 ? 56 : 48);   // AetherSDR: the theme's font
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tips[i]);
        bgMode->addButton(b, modes[i]);
        h1->addWidget(b);
    }
    bgMode->setExclusive(true);
    connect(bgMode, &QButtonGroup::idClicked, this, [this](int m) { setMode(m); });
    h1->addSpacing(6);
    cbBand = new QComboBox;
    cbBand->setFixedWidth(74);   // AetherSDR: room for the arrow
    h1->addWidget(cbBand);
    cbFreq = new QComboBox;
    cbFreq->setEditable(true);
    cbFreq->setFixedWidth(112);
    cbFreq->setToolTip("The dial frequency, kHz (USB). Type another one and Enter: kept for this band");
    h1->addWidget(cbFreq);
    h1->addWidget(new QLabel("kHz"));
    h1->addStretch(1);
    lbUtc = new QLabel("--:--:--");
    QFont fu = lbUtc->font();
    fu.setPointSize(fu.pointSize() + 4);
    fu.setBold(true);
    lbUtc->setFont(fu);
    h1->addWidget(lbUtc);
    v->addLayout(h1);
    connect(cbBand, QOverload<int>::of(&QComboBox::activated), this, &Js8Panel::onBand);
    connect(cbFreq->lineEdit(), &QLineEdit::editingFinished, this, &Js8Panel::onFreqEdited);
    connect(cbFreq, QOverload<int>::of(&QComboBox::activated), this, [this](int) { onFreqEdited(); });

    // the period, DT, the level
    QHBoxLayout *h2 = new QHBoxLayout;
    pbPeriod = new QProgressBar;
    pbPeriod->setRange(0, 1000);
    pbPeriod->setTextVisible(false);
    pbPeriod->setFixedHeight(10);
    h2->addWidget(pbPeriod, 1);
    lbDt = new QLabel("DT --");
    lbDt->setToolTip("The average DT of the stations heard: about 0 when the time is right");
    lbDt->setMinimumWidth(80);
    h2->addWidget(lbDt);
    lbLevel = new QLabel("In --");
    lbLevel->setToolTip("The noise at the decoder input before its level setting, dBFS (the receiver IQ);\n"
                        "t: the automatic time correction");
    lbLevel->setMinimumWidth(90);
    h2->addWidget(lbLevel);
    v->addLayout(h2);

    QHBoxLayout *h3 = new QHBoxLayout;
    lbStat = new QLabel("Waiting for the first period");
    h3->addWidget(lbStat, 1);
    lbRx = new QLabel;
    lbRx->setToolTip("The RX offset (Hz): decoded first; click a line of Band activity to set it");
    h3->addWidget(lbRx);
    chbHb = new QCheckBox("HB");
    chbHb->setToolTip("Messages: show the heartbeats, CQs and their automatic replies (HEARTBEAT SNR)\n"
                      "too - most of the JS8 traffic. Band activity and Stations show them always.");
    chbHb->setFocusPolicy(Qt::NoFocus);
    h3->addWidget(chbHb);
    connect(chbHb, &QCheckBox::toggled, this, [this]() { refreshMessages(); });
    pbClear = new QPushButton("Clear");
    pbClear->setFocusPolicy(Qt::NoFocus);
    h3->addWidget(pbClear);
    pbSettings = new QPushButton("Settings");
    pbSettings->setCheckable(true);
    pbSettings->setFocusPolicy(Qt::NoFocus);
    h3->addWidget(pbSettings);
    v->addLayout(h3);

    // the settings
    gbSet = new QGroupBox("JS8 settings");
    QGridLayout *g = new QGridLayout(gbSet);
    g->setContentsMargins(6, 10, 6, 6);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(4);
    g->addWidget(new QLabel("My call:"), 0, 0);
    leCall = new QLineEdit;
    leCall->setMaxLength(12);
    leCall->setObjectName("Js8MyCall");
    leCall->setValidator(new FtLatinValidator("[A-Za-z0-9/]{0,12}", leCall));   // AetherSDR (the Russian layout)
    g->addWidget(leCall, 0, 1);
    g->addWidget(new QLabel("My locator:"), 0, 2);
    leGrid = new QLineEdit;
    leGrid->setMaxLength(6);
    leGrid->setObjectName("Js8MyLocator");
    leGrid->setValidator(new FtLatinValidator("[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2})?", leGrid));   // AetherSDR
    g->addWidget(leGrid, 0, 3);
    g->addWidget(new QLabel("Search, Hz:"), 1, 0);
    QHBoxLayout *hr = new QHBoxLayout;
    sbFa = new QSpinBox;
    sbFa->setRange(100, 3000);
    sbFa->setSingleStep(50);
    sbFb = new QSpinBox;
    sbFb->setRange(500, 4000);
    sbFb->setSingleStep(50);
    hr->addWidget(sbFa);
    hr->addWidget(new QLabel("-"));
    hr->addWidget(sbFb);
    g->addLayout(hr, 1, 1);
    g->addWidget(new QLabel("Time corr., ms:"), 1, 2);
    sbOffset = new QSpinBox;
    sbOffset->setRange(-10000, 10000);
    sbOffset->setSingleStep(10);
    sbOffset->setToolTip("Added to the time of the received samples; set it so that DT of the stations is about 0");
    g->addWidget(sbOffset, 1, 3);
    chbPsk = new QCheckBox("PSK Reporter");
    chbPsk->setToolTip("Spots of the stations heard to pskreporter.info, mode JS8 (needs my call and locator)");
    g->addWidget(chbPsk, 2, 0, 1, 2);
    g->addWidget(new QLabel("Antenna:"), 2, 2);
    leAnt = new QLineEdit;
    leAnt->setMaxLength(60);
    g->addWidget(leAnt, 2, 3);
    chbAutoT = new QCheckBox("Auto time correction");
    chbAutoT->setToolTip("The time correction found by the program itself: the DT of the stations to 0;\n"
                         "with no decodes it searches some seconds around. Added to Time corr.");
    g->addWidget(chbAutoT, 3, 0, 1, 2);
    g->addWidget(new QLabel("Rig:"), 3, 2);
    leRig = new QLineEdit;
    leRig->setMaxLength(60);
    leRig->setToolTip("The receiver for PSK Reporter (the station information of the spots)");
    g->addWidget(leRig, 3, 3);
    lbPsk = new QLabel("PSK Reporter: off");
    chbEarly = new QCheckBox("Early decode");
    chbEarly->setToolTip("A first decode at 90 % of the signal (Normal 11.8 s, Fast 7.3 s, Turbo 3.7 s,\n"
                         "Slow 23.2 s): most frames shown before the period ends; the full decode adds\n"
                         "the weak ones (the same frame is shown once)");
    g->addWidget(chbEarly, 4, 0, 1, 2);
    chbMiles = new QCheckBox("Distances in miles");
    chbMiles->setToolTip("The distances of the stations in miles (off: km)");
    g->addWidget(chbMiles, 4, 2, 1, 2);
    g->addWidget(lbPsk, 5, 0, 1, 4);
    addTxSettings(g, 6);                // the transmitter (Js8PanelTx.cpp)
    connect(chbEarly, &QCheckBox::toggled, this, &Js8Panel::applySettings);
    connect(chbMiles, &QCheckBox::toggled, this, [this]() {
        twSta->horizontalHeaderItem(3)->setText(chbMiles->isChecked() ? "mi" : "km");
        refreshStations();
    });
    v->addWidget(gbSet);
    gbSet->setVisible(false);
    connect(pbSettings, &QPushButton::toggled, gbSet, &QWidget::setVisible);
    connect(psk, &PskReporter::status, lbPsk, &QLabel::setText);
    for(QLineEdit *e : { leCall, leGrid, leAnt, leRig })
        connect(e, &QLineEdit::editingFinished, this, &Js8Panel::applySettings);
    connect(leCall, &QLineEdit::textEdited, this, [this](const QString &t) {
        const int c = leCall->cursorPosition();
        leCall->setText(t.toUpper());
        leCall->setCursorPosition(c);
    });
    connect(chbPsk, &QCheckBox::toggled, this, &Js8Panel::applySettings);
    connect(chbAutoT, &QCheckBox::toggled, this, &Js8Panel::applySettings);
    for(QSpinBox *s : { sbFa, sbFb, sbOffset })
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, &Js8Panel::applySettings);

    // band activity, the messages, the stations
    QSplitter *sp = new QSplitter(Qt::Vertical);
    sp->setChildrenCollapsible(false);
    auto section = [sp](const QString &title, QWidget *w) {
        QWidget *box = new QWidget;
        QVBoxLayout *vb = new QVBoxLayout(box);
        vb->setContentsMargins(0, 0, 0, 0);
        vb->setSpacing(2);
        vb->addWidget(new QLabel(title));
        vb->addWidget(w);
        sp->addWidget(box);
    };
    twBand = new QTableWidget(0, 4);
    twBand->setHorizontalHeaderLabels(QStringList() << "Hz" << "UTC" << "dB" << "Text");
    section("Band activity", twBand);
    pteMsg = new QPlainTextEdit;
    pteMsg->setReadOnly(true);
    pteMsg->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    {
        QFont f("Consolas");
        f.setStyleHint(QFont::Monospace);
        f.setPointSize(9);
        pteMsg->setFont(f);
        QPalette pal = pteMsg->palette();
        pal.setColor(QPalette::Text, QColor(230, 230, 230));
        pal.setColor(QPalette::Base, QColor(22, 22, 22));
        pteMsg->setPalette(pal);
    }
    section("Messages", pteMsg);
    twSta = new QTableWidget(0, 8);
    twSta->setHorizontalHeaderLabels(QStringList() << "Call" << "Locator" << "Country" << "km" << QString::fromUtf8("Az°")
                                                   << "dB" << "Hz" << "Last");
    section("Stations heard", twSta);
    sp->setStretchFactor(0, 3);
    sp->setStretchFactor(1, 3);
    sp->setStretchFactor(2, 2);
    v->addWidget(sp, 1);
    buildTxUi(v);                       // the outgoing text, Send, CQ, HB, ... (Js8PanelTx.cpp)
    for(QTableWidget *t : { twBand, twSta })
    {
        // the whole line copied (Ctrl+C, the right mouse button): the cells with tabs between
        auto copyRows = [t]() {
            QStringList lines;
            const QModelIndexList rows = t->selectionModel()->selectedRows();
            for(const QModelIndex &mi : rows)
            {
                QStringList cells;
                for(int c = 0; c < t->columnCount(); c++)
                    cells << (t->item(mi.row(), c) ? t->item(mi.row(), c)->text() : QString());
                lines << cells.join('\t');
            }
            if(!lines.isEmpty())
                QGuiApplication::clipboard()->setText(lines.join('\n'));
        };
        QAction *aCopy = new QAction("Copy line", t);
        aCopy->setShortcut(QKeySequence::Copy);
        aCopy->setShortcutContext(Qt::WidgetShortcut);
        connect(aCopy, &QAction::triggered, t, copyRows);
        t->addAction(aCopy);
        t->setContextMenuPolicy(Qt::ActionsContextMenu);
        t->verticalHeader()->setVisible(false);
        t->verticalHeader()->setDefaultSectionSize(18);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setSelectionBehavior(QAbstractItemView::SelectRows);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
        t->setAlternatingRowColors(true);
        t->setWordWrap(false);
        t->horizontalHeader()->setHighlightSections(false);
        QFont f("Consolas");
        f.setStyleHint(QFont::Monospace);
        f.setPointSize(9);
        t->setFont(f);
        QPalette pal = t->palette();
        pal.setColor(QPalette::Text, QColor(230, 230, 230));
        pal.setColor(QPalette::Base, QColor(22, 22, 22));
        pal.setColor(QPalette::AlternateBase, QColor(30, 30, 30));
        t->setPalette(pal);
    }
    const int wBand[4] = { 44, 62, 34, 300 };
    for(int i = 0; i < 4; i++)
        twBand->setColumnWidth(i, wBand[i]);
    twBand->horizontalHeader()->setStretchLastSection(true);
    const int wSta[8] = { 90, 56, 130, 50, 36, 34, 44, 62 };
    for(int i = 0; i < 8; i++)
        twSta->setColumnWidth(i, wSta[i]);
    twSta->horizontalHeader()->setStretchLastSection(true);
    twSta->setSortingEnabled(true);
    connect(twBand, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem *it = twBand->item(row, 0);
        if(it && !it->text().isEmpty() && !sending)
        {
            rxFreq = it->text().toInt();
            applySettings();
            userAction();
        }
    });
    // Stations: a click selects the station (the text is directed to it), a double click moves
    // the offset to it too
    connect(twSta, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem *it = twSta->item(row, 0);
        if(it)
            selectCall(it->text());
        userAction();
    });
    connect(twSta, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QTableWidgetItem *it = twSta->item(row, 0), *io = twSta->item(row, 6);
        if(it)
            selectCall(it->text());
        if(io && !sending)
        {
            const int off = io->data(Qt::DisplayRole).toInt();
            if(off > 0)
            {
                rxFreq = off;
                applySettings();
            }
        }
        userAction();
    });
    connect(pbClear, &QPushButton::clicked, this, [this]() {
        lines.clear();
        acts.clear();
        stations.clear();
        dtHist.clear();
        refreshBand();
        refreshMessages();
        refreshStations();
    });

    tick = new QTimer(this);
    tick->setInterval(200);
    connect(tick, &QTimer::timeout, this, &Js8Panel::onTick);
    connect(unit, &FtUnit::decoded, this, [this](qint64 p, int m, QVector<FtUnit::Decode> l, int ms) {
        onDecoded(p, m, l, ms, false); }, Qt::QueuedConnection);
    connect(unit, &FtUnit::earlyDecoded, this, [this](qint64 p, int m, QVector<FtUnit::Decode> l, int ms) {
        onDecoded(p, m, l, ms, true); }, Qt::QueuedConnection);
    bgMode->button(curMode)->setChecked(true);
    fillBands();
    // AetherSDR, build 409: the waterfall (see Js8Panel.h). JS8 transmits on
    // the frequency it receives on: one band, set by either mouse button.
    wf = new FtWaterfall(unit, 0);
    wf->hide();
    wf->setSingle(true);
    connect(wf, &FtWaterfall::rxClicked, this, [this](int hz) { rxFreq = hz; applySettings(); });
}

Js8Panel::~Js8Panel()
{
    delete wf;                          // (gone already when the main window took it)
}

void Js8Panel::wfSettings()
{
    if(!wf)
        return;
    wf->setSearch(sbFa->value(), qMax(sbFa->value() + 200, sbFb->value()));
    wf->setMode(curMode);
    wf->setRx(rxFreq);
    wf->setTx(rxFreq);
}

// ---- settings --------------------------------------------------------------------------------------

void Js8Panel::loadSettings()
{
    QSettings st(QDir::homePath() + "/AetherSDR-FTX/Options.ini", QSettings::IniFormat);
    const int ftAuto = st.value("FT/AutoOffsetGoodMs", 0).toInt();
    st.beginGroup("JS8");
    int m = st.value("Mode", FtUnit::JS8N).toInt();
    if(m != FtUnit::JS8N && m != FtUnit::JS8F && m != FtUnit::JS8T && m != FtUnit::JS8S)
        m = FtUnit::JS8N;
    curMode = m;
    band = qBound(0, st.value("Band", 2).toInt(), BANDS - 1);
    for(int b = 0; b < BANDS; b++)
        freqs[b] = st.value(QString("Freq%1").arg(b), defFreq[b]).toLongLong();
    leCall->setText(st.value("MyCall", "").toString());
    leGrid->setText(st.value("MyGrid", "").toString());
    leAnt->setText(st.value("Antenna", "").toString());
    leRig->setText(st.value("Rig", "SDR Micron (N7DDC)").toString());
    sbFa->setValue(st.value("Fa", 200).toInt());
    sbFb->setValue(st.value("Fb", 3500).toInt());
    sbOffset->setValue(st.value("TimeOffsetMs", 0).toInt());
    chbAutoT->setChecked(st.value("AutoTime", true).toBool());
    // the time correction: the one found in the FT window until JS8 has its own (the same clock)
    autoSaved = st.contains("AutoOffsetGoodMs") ? st.value("AutoOffsetGoodMs", 0).toInt()
                                            : ftAuto;
    chbPsk->setChecked(st.value("PskReporter", false).toBool());
    chbHb->setChecked(st.value("ShowHB", false).toBool());
    chbEarly->setChecked(st.value("EarlyDecode", true).toBool());
    chbMiles->setChecked(st.value("Miles", false).toBool());
    rxFreq = st.value("RxFreq", 1500).toInt();
    pbSettings->setChecked(st.value("SettingsOpen", true).toBool());
    winGeom = st.value("WindowGeometry").toByteArray();
    loadTxSettings(st);
    st.endGroup();
    // the first time: my call and locator as in the FT window
    if(leCall->text().isEmpty() && leGrid->text().isEmpty())
    {
        st.beginGroup("FT");
        leCall->setText(st.value("MyCall", "").toString());
        leGrid->setText(st.value("MyGrid", "").toString());
        leAnt->setText(st.value("Antenna", "").toString());
        st.endGroup();
    }
    bgMode->button(curMode)->setChecked(true);
    fillBands();
}

void Js8Panel::saveSettings()
{
    QSettings st(QDir::homePath() + "/AetherSDR-FTX/Options.ini", QSettings::IniFormat);
    st.beginGroup("JS8");
    st.setValue("Mode", curMode);
    st.setValue("Band", band);
    for(int b = 0; b < BANDS; b++)
    {
        if(freqs[b] != defFreq[b])
            st.setValue(QString("Freq%1").arg(b), freqs[b]);
        else
            st.remove(QString("Freq%1").arg(b));
    }
    st.setValue("MyCall", leCall->text().trimmed().toUpper());
    st.setValue("MyGrid", leGrid->text().trimmed());
    st.setValue("Antenna", leAnt->text().trimmed());
    st.setValue("Rig", leRig->text().trimmed());
    st.setValue("Fa", sbFa->value());
    st.setValue("Fb", sbFb->value());
    st.setValue("TimeOffsetMs", sbOffset->value());
    st.setValue("AutoTime", chbAutoT->isChecked());
    st.setValue("AutoOffsetGoodMs", autoSaved);
    st.setValue("PskReporter", chbPsk->isChecked());
    st.setValue("ShowHB", chbHb->isChecked());
    st.setValue("EarlyDecode", chbEarly->isChecked());
    st.setValue("Miles", chbMiles->isChecked());
    st.setValue("RxFreq", rxFreq);
    st.setValue("SettingsOpen", pbSettings->isChecked());
    if(!winGeom.isEmpty())
        st.setValue("WindowGeometry", winGeom);
    saveTxSettings(st);
    st.endGroup();
}

void Js8Panel::showWindow(const QRect &mainGeom)
{
    if(!winGeom.isEmpty() && restoreGeometry(winGeom))
    {
        if(!QGuiApplication::screenAt(geometry().center()))
            winGeom.clear();            // its monitor gone: next to the main window again
    }
    if(winGeom.isEmpty())
    {
        QScreen *scr = QGuiApplication::screenAt(mainGeom.center());
        const QRect a = scr ? scr->availableGeometry() : mainGeom;
        const int w = 820, h = qBound(480, mainGeom.height(), a.height() - 40);
        int x = mainGeom.left() - w - 8;
        if(x < a.left())
            x = qMax(a.left(), mainGeom.left() + 20);
        const int y = qBound(a.top(), mainGeom.top(), a.bottom() - h);
        resize(w, h);
        move(x, y);
    }
    show();
    raise();
    activateWindow();
}

void Js8Panel::hideWindow()
{
    if(isVisible())
        winGeom = saveGeometry();
    hide();
    saveSettings();
}

void Js8Panel::closeEvent(QCloseEvent *e)
{
    winGeom = saveGeometry();
    e->accept();
    emit closed();                      // -> JS8 off (hideWindow)
}

void Js8Panel::applySettings()
{
    if(sbOff && sbOff->value() != rxFreq)
    {
        sbOff->blockSignals(true);
        sbOff->setValue(rxFreq);
        sbOff->blockSignals(false);
    }
    wfSettings();                       // AetherSDR, build 409
    if(!running)
        return;                         // the unit belongs to the window that runs
    FtUnit::Settings s;
    s.mode = curMode;
    s.depth = 2;
    s.ap = false;
    s.nfa = sbFa->value();
    s.nfb = qMax(sbFa->value() + 200, sbFb->value());
    s.nfqso = rxFreq;
    s.mycall = leCall->text().trimmed().toUpper();
    s.timeOffsetMs = sbOffset->value();
    s.autoTime = chbAutoT->isChecked();
    s.early = chbEarly->isChecked();
    unit->setSettings(s);
    lbRx->setText(QString("RX %1 Hz").arg(rxFreq));
    QString grid = leGrid->text().trimmed();
    if(grid.size() >= 4)
        grid = grid.left(2).toUpper() + grid.mid(2, 2) + grid.mid(4).toLower();
    psk->setLocalStation(s.mycall, grid, leAnt->text(), leRig->text());
    psk->setEnabled(running && chbPsk->isChecked());
    if(chbPsk->isChecked() && (s.mycall.isEmpty() || grid.size() < 4))
        lbPsk->setText("PSK Reporter: my call and locator are needed");
}

// ---- submode, band, frequency ----------------------------------------------------------------------

void Js8Panel::fillBands()
{
    cbBand->blockSignals(true);
    cbBand->clear();
    for(int b = 0; b < BANDS; b++)
        cbBand->addItem(bandName[b], b);
    cbBand->setCurrentIndex(qMax(0, cbBand->findData(band)));
    cbBand->blockSignals(false);
    cbFreq->blockSignals(true);
    cbFreq->clear();
    cbFreq->addItem(QString::number(freqs[band] / 1000.0, 'f', 3));
    if(freqs[band] != defFreq[band])
        cbFreq->addItem(QString::number(defFreq[band] / 1000.0, 'f', 3) + " (default)");
    cbFreq->setCurrentIndex(0);
    cbFreq->blockSignals(false);
}

void Js8Panel::setMode(int m)
{
    if(m != FtUnit::JS8N && m != FtUnit::JS8F && m != FtUnit::JS8T && m != FtUnit::JS8S)
        return;
    const bool changed = (m != curMode);
    curMode = m;
    if(bgMode->button(m) && !bgMode->button(m)->isChecked())
        bgMode->button(m)->setChecked(true);
    if(!changed)
        return;
    applySettings();
    emit modeChanged(curMode);
}

void Js8Panel::onBand(int index)
{
    band = cbBand->itemData(index).toInt();
    fillBands();
    if(running)
        tuneBand();
}

void Js8Panel::onFreqEdited()
{
    QString t = cbFreq->currentText();
    t = t.left(t.indexOf(' ') < 0 ? t.size() : t.indexOf(' ')).trimmed();
    t.replace(',', '.');
    bool ok = false;
    const double khz = t.toDouble(&ok);
    if(ok && khz >= 1000.0 && khz <= 55000.0)
    {
        const qint64 hz = qRound64(khz * 1000.0);
        if(hz != freqs[band])
        {
            freqs[band] = hz;
            fillBands();
            if(running)
                tuneBand();
        }
    }
    else
        fillBands();
}

void Js8Panel::tuneBand()
{
    if(freqs[band] > 0)
        emit tuneRequest(freqs[band]);
}

void Js8Panel::setDialShown(qint64 hz)
{
    dial = hz;
}

void Js8Panel::start()
{
    running = true;
    unit->setAutoOffset(autoSaved);
    applySettings();
    unit->setActive(true);
    if(wf)
        wf->setRunning(true);           // AetherSDR, build 409
    tuneBand();
    tick->start();
    txTick->start();
    userAction();
    lbStat->setText(unit->errorJs8().isEmpty() ? QString("Waiting for the first period") : unit->errorJs8());
}

void Js8Panel::stop()
{
    if(running)
        autoSaved = unit->autoGoodMs();
    running = false;
    haltTx();                           // (MOX off)
    txTick->stop();
    unit->setActive(false);
    if(wf)
        wf->setRunning(false);          // AetherSDR, build 409
    psk->setEnabled(false);
    tick->stop();
    saveSettings();
}

void Js8Panel::onTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    lbUtc->setText(QDateTime::fromMSecsSinceEpoch(now, Qt::UTC).toString("HH:mm:ss"));
    const int P = FtUnit::periodMs(curMode);
    const int corr = sbOffset->value() + (chbAutoT->isChecked() ? unit->autoOffsetMs() : 0);
    pbPeriod->setValue((int)(((now + corr) % P + P) % P * 1000 / P));
    QString lv = QString("In %1 dBFS").arg(unit->levelDb(), 0, 'f', 0);
    if(chbAutoT->isChecked())
        lv += unit->autoSearching() ? QString("  t %1 s?").arg(unit->autoOffsetMs() / 1000.0, 0, 'f', 1)
                                    : QString("  t %1%2 s").arg(unit->autoOffsetMs() >= 0 ? "+" : "").arg(unit->autoOffsetMs() / 1000.0, 0, 'f', 2);
    lbLevel->setText(lv);
}

// ---- decodes ---------------------------------------------------------------------------------------

int Js8Panel::actKey(int off) const
{
    int best = off, bestD = 11;
    for(auto it = acts.constBegin(); it != acts.constEnd(); ++it)
    {
        const int d = qAbs(it.key() - off);
        if(d < bestD)
        {
            bestD = d;
            best = it.key();
        }
    }
    return best;
}

void Js8Panel::onDecoded(qint64 periodStartMs, int mode, QVector<FtUnit::Decode> list, int decodeMs, bool early)
{
    if(!running || !FtUnit::isJs8(mode))
        return;
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(periodStartMs, Qt::UTC);
    if(decodeMs < 0)
    {
        lbStat->setText(QString("%1 %2: not enough audio, waiting").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss")));
        return;
    }
    if(txPeriodsMs.contains(periodStartMs))
    {
        // my transmission: not decoded (no own signal in the band activity, no spots of it)
        lbStat->setText(QString("%1 %2: my transmission").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss")));
        return;
    }
    // the early and the full decode of a period bring the same frames: each once (the same frame
    // within 10 Hz)
    if(seenPeriod != periodStartMs)
    {
        seenPeriod = periodStartMs;
        seenFrames.clear();
        earlyCount = 0;
    }
    for(int i = 0; i < list.size(); )
    {
        const int off = (int)lround(list[i].freq);
        bool dup = false;
        for(const auto &f : seenFrames)
            if(f.first == list[i].msg && qAbs(f.second - off) <= 10)
            {
                dup = true;
                break;
            }
        if(dup)
            list.remove(i);
        else
        {
            seenFrames.append(qMakePair(list[i].msg, off));
            i++;
        }
    }
    if(early)
    {
        earlyCount = list.size();
        lbStat->setText(QString("%1 %2: %3 frames (early), %4 ms").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss"))
                        .arg(list.size()).arg(decodeMs));
    }
    else if(earlyCount > 0)
        lbStat->setText(QString("%1 %2: %3 frames (early %4), %5 ms").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss"))
                        .arg(list.size() + earlyCount).arg(earlyCount).arg(decodeMs));
    else
        lbStat->setText(QString("%1 %2: %3 frames, %4 ms").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss"))
                        .arg(list.size()).arg(decodeMs));
    const int nutc = t.time().hour() * 10000 + t.time().minute() * 100 + t.time().second();
    static const QRegularExpression gridRe("^[A-R]{2}[0-9]{2}([A-X]{2})?$");

    std::sort(list.begin(), list.end(), [](const FtUnit::Decode &a, const FtUnit::Decode &b) { return a.freq < b.freq; });
    for(const FtUnit::Decode &d : list)
    {
        dtHist.append(d.dt);
        if(dtHist.size() > 60)
            dtHist.removeFirst();
        const int off = (int)lround(d.freq);
        DecodedText::Js8Decode jd { d.msg, d.bits, submodeOf(mode), d.qual, nutc, off, d.snr, d.dt };
        const DecodedText dt(jd);
        QString text = dt.message();
        if(dt.isLowConfidence())
            text = "[" + text.trimmed() + "] ";
        const bool first = (d.bits & Varicode::JS8CallFirst) == Varicode::JS8CallFirst;
        const bool last = (d.bits & Varicode::JS8CallLast) == Varicode::JS8CallLast;
        if(last)
            text = Varicode::rstrip(text) + EOT_MARK;
        qDebug().noquote() << "JS8:" << t.toString("HH:mm:ss") << d.snr << QString::number(d.dt, 'f', 1) << off
                           << "[" + d.msg + "]" << d.bits << text;

        // Messages: the frames of a message one after the other at the same offset (+-10 Hz)
        int li = -1;
        if(!first)
        {
            for(int i = lines.size() - 1; i >= 0 && i >= lines.size() - 50; i--)
            {
                if(!lines[i].done && qAbs(lines[i].off - off) <= 10 && lines[i].t.secsTo(t) <= 180)
                {
                    li = i;
                    break;
                }
            }
        }
        if(li < 0)
        {
            Line l;
            l.t = t;
            l.off = off;
            l.snr = d.snr;
            l.done = false;
            l.hb = dt.isHeartbeat() ||
                   (dt.isDirectedMessage() && dt.directedMessage().value(2).contains("HEARTBEAT"));
            if(dt.isDirectedMessage())
            {
                const QString to = dt.directedMessage().value(1), my = myCall();
                l.toMe = !my.isEmpty() && (to == my || to == baseCall(my));
            }
            lines.append(l);
            li = lines.size() - 1;
        }
        lines[li].text += text;
        lines[li].snr = d.snr;
        lines[li].off = off;
        if(last)
            lines[li].done = true;
        while(lines.size() > 400)
            lines.removeFirst();

        // band activity
        const int k = actKey(off);
        Act a = acts.value(k);
        acts.remove(k);
        a.last = t;
        a.snr = d.snr;
        if(first && !a.text.isEmpty() && !a.text.endsWith(EOT_MARK.trimmed()) && !a.text.endsWith(EOT_MARK))
            a.text += "  ";
        a.text += text;
        if(a.text.size() > 300)
            a.text = a.text.right(300);
        acts.insert(off, a);

        // the callsigns: heartbeats / CQs, compound frames (the first word), directed messages (from)
        QString call, grid;
        if(dt.isHeartbeat() || dt.isCompound())
        {
            call = dt.compoundCall();
            const QString ex = dt.extra().trimmed().toUpper();
            if(gridRe.match(ex.left(6)).hasMatch() || gridRe.match(ex.left(4)).hasMatch())
                grid = ex.left(ex.size() >= 6 && gridRe.match(ex.left(6)).hasMatch() ? 6 : 4);
        }
        else if(dt.isDirectedMessage())
            call = dt.directedMessage().value(0);
        call = call.trimmed().toUpper();
        if(!call.isEmpty() && !call.startsWith('@') && !call.contains("<") && CtyDat::isCall(call))
        {
            addStation(call, grid, d.snr, off, t);
            if(dial > 0)
                psk->addSpot(call, grid.left(4).size() == 4 ? grid : QString(), dial + off, "JS8", d.snr, t);
        }
        // the transmitter: the commands to me, the heartbeats (the automatic replies)
        heard(d, dt, QDateTime::currentMSecsSinceEpoch());
    }
    // the activity older than 30 min out
    for(auto it = acts.begin(); it != acts.end(); )
        it = (it.value().last.secsTo(t) > 1800) ? acts.erase(it) : it + 1;
    if(!dtHist.isEmpty())
    {
        double s = 0;
        for(double x : dtHist)
            s += x;
        const double avg = s / dtHist.size();
        const double lim = (curMode == FtUnit::JS8T) ? 0.1 : 0.3;
        lbDt->setText(QString("DT %1%2 s").arg(avg >= 0 ? "+" : "").arg(avg, 0, 'f', 2));
        lbDt->setStyleSheet(fabs(avg) > lim ? "color: #FF8080;" : "color: #90E090;");
    }
    refreshBand();
    refreshMessages();
    refreshStations();
}

void Js8Panel::refreshBand()
{
    twBand->setRowCount(acts.size());
    int r = 0;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for(auto it = acts.constBegin(); it != acts.constEnd(); ++it, ++r)
    {
        const Act &a = it.value();
        const bool fresh = a.last.secsTo(now) < 120;
        const QStringList cells = { QString::number(it.key()), a.last.toString("HH:mm:ss"), QString::number(a.snr),
                                    a.text.right(160) };
        for(int c = 0; c < 4; c++)
        {
            QTableWidgetItem *x = new QTableWidgetItem(cells[c]);
            x->setForeground(fresh ? QColor(230, 230, 230) : QColor(130, 130, 130));
            if(c < 3)
                x->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if(c == 3)
                x->setToolTip(a.text);
            if(qAbs(it.key() - rxFreq) <= 10)
                x->setBackground(QColor(28, 60, 90));
            twBand->setItem(r, c, x);
        }
    }
}

void Js8Panel::refreshMessages()
{
    QScrollBar *sb = pteMsg->verticalScrollBar();
    const bool atEnd = sb->value() >= sb->maximum() - 2;
    // my transmissions orange (as the TX of JS8Call), the messages to me yellow
    QStringList out;
    const int from = qMax(0, lines.size() - 300);
    for(int i = from; i < lines.size(); i++)
    {
        const Line &l = lines[i];
        if(l.hb && !chbHb->isChecked() && !l.tx && !l.toMe)
            continue;
        QString head = QString("%1 %2 %3  ").arg(l.t.toString("HH:mm:ss")).arg(l.tx ? QString(" TX") : QString::number(l.snr), 3)
                                             .arg(l.off, 4);
        head.replace(' ', "&nbsp;");
        const QString col = l.tx ? "#FFB060" : l.toMe ? "#FFF07A" : "#E6E6E6";
        out << QString("<span style=\"color:%1\">%2%3</span>").arg(col, head, l.text.trimmed().toHtmlEscaped());
    }
    pteMsg->clear();
    if(!out.isEmpty())
        pteMsg->appendHtml(out.join("<br>"));
    if(atEnd)
        sb->setValue(sb->maximum());
}

void Js8Panel::addStation(const QString &call, const QString &grid, int snr, int off, const QDateTime &t)
{
    Station &s = stations[call];
    if(s.call.isEmpty())
    {
        s.call = call;
        s.bestSnr = snr;
        s.count = 0;
        s.km = -1;
        s.az = -1;
        if(const CtyDat::Entity *e = CtyDat::instance().entityOf(call))
        {
            s.country = e->name;
            s.cont = e->cont;
        }
    }
    if(!grid.isEmpty())
        s.grid = grid;
    s.lastSnr = snr;
    s.bestSnr = qMax(s.bestSnr, snr);
    s.count++;
    s.off = off;
    s.last = t;
    double myLat, myLon, lat, lon;
    bool pos = false;
    if(!s.grid.isEmpty())
        pos = CtyDat::gridToLatLon(s.grid, lat, lon);
    else if(const CtyDat::Entity *e = CtyDat::instance().entityOf(call))
    {
        lat = e->lat;
        lon = e->lon;
        pos = true;
    }
    if(pos && CtyDat::gridToLatLon(leGrid->text().trimmed(), myLat, myLon))
        CtyDat::distAz(myLat, myLon, lat, lon, s.km, s.az);
}

void Js8Panel::refreshStations()
{
    twSta->setSortingEnabled(false);
    QList<Station> all = stations.values();
    std::sort(all.begin(), all.end(), [](const Station &a, const Station &b) { return a.last > b.last; });
    if(all.size() > 400)
        all = all.mid(0, 400);
    twSta->setRowCount(all.size());
    for(int r = 0; r < all.size(); r++)
    {
        const Station &s = all[r];
        auto num = [](double v) {
            QTableWidgetItem *it = new QTableWidgetItem;
            if(v > -1e8)
                it->setData(Qt::DisplayRole, qRound(v));
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return it;
        };
        twSta->setItem(r, 0, new QTableWidgetItem(s.call));
        twSta->setItem(r, 1, new QTableWidgetItem(s.grid));
        QTableWidgetItem *ct = new QTableWidgetItem(s.country);
        if(!s.cont.isEmpty())
            ct->setToolTip(s.country + ", " + s.cont);
        twSta->setItem(r, 2, ct);
        twSta->setItem(r, 3, num(s.km >= 0 ? (chbMiles->isChecked() ? s.km * 0.621371 : s.km) : -1e9));
        twSta->setItem(r, 4, num(s.az >= 0 ? s.az : -1e9));
        twSta->setItem(r, 5, num(s.lastSnr));
        twSta->setItem(r, 6, num(s.off));
        QTableWidgetItem *tl = new QTableWidgetItem(s.last.toString("HH:mm:ss"));
        tl->setToolTip(QString("%1 decodes, best %2 dB").arg(s.count).arg(s.bestSnr));
        twSta->setItem(r, 7, tl);
    }
    twSta->setSortingEnabled(true);
}
