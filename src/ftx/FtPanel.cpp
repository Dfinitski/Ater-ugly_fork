// ANAN fork build 381: this file is MicronSDR's (N7DDC), taken as it is for the
// FTX mode of this program; the changes for it are marked "AetherSDR".
#define _USE_MATH_DEFINES   // AetherSDR: M_PI under MSVC
// FT8 / FT4 / WSPR receiver window (MicronSDR FTX Decoder): see FtPanel.h
#include "FtPanel.h"
#include "core/ForkName.h"
#include "PskReporter.h"
#include "FtWaterfall.h"
#include <QtWidgets>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>
#include <QAbstractItemView>
#include <algorithm>

const char *FtPanel::bandName[FtPanel::BANDS] = { "160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m",
                                                   "4m", "2m", "1.25m", "70cm", "23cm" };   // AetherSDR, 390: transverters
const qint64 FtPanel::defFreq[3][FtPanel::BANDS] = {
    { 1840000, 3573000, 5357000, 7074000, 10136000, 14074000, 18100000, 21074000, 24915000, 28074000, 50313000,
      70154000, 144174000, 222065000, 432174000, 1296174000 },   // FT8
    { 0,       3575000, 5357000, 7080000, 10140000, 14080000, 18104000, 21140000, 24919000, 28180000, 50318000,
      0,        144170000, 0,         432170000, 0 },   // FT4
    { 1836600, 3568600, 5287200, 7038600, 10138700, 14095600, 18104600, 21094600, 24924600, 28124600, 50293000,
      70091000, 144489000, 0,         432300000, 1296500000 } }; // WSPR

// other frequencies in use (the list of the frequency box): mode index (FT8, FT4, WSPR), band, Hz, note
static const struct { int mode, band; qint64 hz; const char *note; } ALT_FREQ[] = {
    { 0, 0,  1908000, "R2" },        { 0, 0,  1810000, "R1" },
    { 0, 1,  3567000, "DXpedition" },{ 0, 1,  3585000, "" },
    { 0, 2,  5362000, "" },
    { 0, 3,  7071000, "" },          { 0, 3,  7056000, "DXpedition" }, { 0, 3,  7080000, "" },
    { 0, 4, 10131000, "DXpedition" },{ 0, 4, 10133000, "" },          { 0, 4, 10143000, "" },
    { 0, 5, 14071000, "" },          { 0, 5, 14090000, "DXpedition" },
    { 0, 6, 18095000, "DXpedition" },
    { 0, 7, 21091000, "DXpedition" },
    { 0, 8, 24911000, "DXpedition" },
    { 0, 9, 28091000, "DXpedition" },{ 0, 9, 28095000, "" },
    { 1, 1,  3568000, "R3" },
    { 1, 3,  7047500, "WSJT-X" },
    { 2, 1,  3592600, "R2" },        { 2, 2,  5364700, "" },
    { 0, 11, 70100000, "" },         { 0, 14, 432065000, "" },      // AetherSDR, 390
};

// AetherSDR: no stylesheet of its own -- the window takes the program's theme
// (applyAppTheme() where it is created).

FtPanel::FtPanel(FtUnit *u, QWidget *parent)
    : QWidget(parent), unit(u), curMode(FtUnit::FT8), dial(0), running(false), rxFreq(1500), txFreq(1500),
      autoSaved(0), ctyLoaded(false), lastPeriodShown(0)
{
    setObjectName("FtPanel");
    setWindowFlags(Qt::Window);         // a window of its own (owned by the main window)
    setWindowTitle("AetherSDR FTX");   // AetherSDR
    setMinimumSize(726, 260);   // AetherSDR, build 408: the operator's smallest width (screenshot, 14.11)
    setAttribute(Qt::WA_StyledBackground, true);
    psk = new PskReporter(AetherSDR::forkDisplayVersion(), this);   // (396: the decoding software named in the spots)
    for(int m = 0; m < 3; m++)
    {
        bandOf[m] = 5;                      // 20 m
        for(int b = 0; b < BANDS; b++)
            freqs[m][b] = defFreq[m][b];
    }

    QVBoxLayout *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 4, 6, 4);
    v->setSpacing(6);   // AetherSDR: 4 -- the rows ran into each other with the larger font

    // the mode, the band, the dial frequency
    QHBoxLayout *h1 = new QHBoxLayout;
    bgMode = new QButtonGroup(this);
    const int modes[3] = { FtUnit::FT8, FtUnit::FT4, FtUnit::WSPR };
    for(int i = 0; i < 3; i++)
    {
        QPushButton *b = new QPushButton(FtUnit::modeName(modes[i]));
        b->setCheckable(true);
        b->setMinimumWidth(46);   // AetherSDR: no fixed width -- the theme's font and padding cut "WSPR"
        b->setFocusPolicy(Qt::NoFocus);
        bgMode->addButton(b, modes[i]);
        h1->addWidget(b);
    }
    bgMode->setExclusive(true);
    // AetherSDR, build 406: the JS8 sub-mode -- its own window (MicronSDR: the JS8 button)
    {
        QPushButton *bJs8 = new QPushButton("JS8");
        bJs8->setMinimumWidth(46);
        bJs8->setFocusPolicy(Qt::NoFocus);
        bJs8->setToolTip("JS8 (JS8Call): its own window instead of this one");
        connect(bJs8, &QPushButton::clicked, this, [this]() { emit js8Requested(); });
        h1->addWidget(bJs8);
    }
    connect(bgMode, &QButtonGroup::idClicked, this, [this](int m) { setMode(m); });
    h1->addSpacing(10);
    cbBand = new QComboBox;
    cbBand->setFixedWidth(74);   // AetherSDR, 390: room for the arrow
    h1->addWidget(cbBand);
    cbFreq = new QComboBox;
    cbFreq->setEditable(true);
    cbFreq->setFixedWidth(112);
    cbFreq->view()->setMinimumWidth(200);   // AetherSDR, 390: the notes of the list
    cbFreq->setToolTip("The dial frequency, kHz (USB). Type another one and Enter: kept for this band and mode");
    h1->addWidget(cbFreq);
    h1->addWidget(new QLabel("kHz"));
    h1->addStretch(1);
    lbRx = new QLabel;
    lbRx->setToolTip("The RX frequency (Hz): decoded first and with a priori information;\n"
                     "the left mouse button on the waterfall or a click on a decode sets it");
    lbRx->setStyleSheet("color: #70E070;");
    h1->addWidget(lbRx);
    lbTx = new QLabel;
    lbTx->setToolTip("The TX frequency (Hz): the right mouse button on the waterfall sets it");
    lbTx->setStyleSheet("color: #FF7070;");
    h1->addWidget(lbTx);
    h1->addSpacing(8);
    pbSettings = new QPushButton("Settings");
    pbSettings->setCheckable(true);
    pbSettings->setFocusPolicy(Qt::NoFocus);
    pbSettings->setToolTip("The settings of the mode");
    h1->addWidget(pbSettings);
    h1->addSpacing(8);
    lbUtc = new QLabel("--:--:--");
    QFont fu = lbUtc->font();
    fu.setPointSize(fu.pointSize() + 4);
    fu.setBold(true);
    lbUtc->setFont(fu);
    h1->addWidget(lbUtc);
    v->addLayout(h1);
    connect(cbBand, QOverload<int>::of(&QComboBox::activated), this, &FtPanel::onBand);
    connect(cbFreq->lineEdit(), &QLineEdit::editingFinished, this, &FtPanel::onFreqEdited);
    connect(cbFreq, QOverload<int>::of(&QComboBox::activated), this, [this](int) { onFreqEdited(); });

    // the period; the state, DT, the level
    pbPeriod = new QProgressBar;
    pbPeriod->setRange(0, 1000);
    pbPeriod->setTextVisible(false);
    pbPeriod->setFixedHeight(6);
    v->addWidget(pbPeriod);
    QHBoxLayout *h2 = new QHBoxLayout;
    lbStat = new QLabel("Waiting for the first period");
    lbStat->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // AetherSDR: a long text is cut, not pushed over the others
    h2->addWidget(lbStat, 1);
    lbDt = new QLabel("DT --");
    lbDt->setToolTip("The average DT of the stations heard: about 0 when the time of this computer\n"
                     "and the time correction (Settings) are right (FT4 within +-0.15 s, WSPR +-1 s)");
    lbDt->setMinimumWidth(96);
    h2->addWidget(lbDt);
    lbLevel = new QLabel("In --");
    lbLevel->setToolTip("The decoder input before its level setting (the receiver IQ), dBFS:\n"
                        "In - the noise in 3000 Hz (the median of the spectrum of the period: the signals left out),\n"
                        "pk - the peak of the period; new at each decode (WSPR: every 2 min).\n"
                        "ATT Auto steps wait for the end of the period (after the decoding point)");
    lbLevel->setMinimumWidth(240);
    h2->addWidget(lbLevel);
    pbQrz = new QPushButton("QRZ");
    pbQrz->setFocusPolicy(Qt::NoFocus);
    h2->addWidget(pbQrz);
    pbClear = new QPushButton("Clear");
    pbClear->setFocusPolicy(Qt::NoFocus);
    pbClear->setToolTip("The band activity cleared");
    h2->addWidget(pbClear);
    v->addLayout(h2);

    // the transmitter: the DX, Enable TX, Tx1 .. Tx6 (FtPanelTx.cpp)
    buildTxUi(v);

    // settings of the mode
    gbSet = new QGroupBox("Settings");
    QGridLayout *g = new QGridLayout(gbSet);
    g->setContentsMargins(6, 10, 6, 6);
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(4);
    g->addWidget(new QLabel("My call:"), 0, 0);
    leCall = new QLineEdit;
    leCall->setFixedWidth(156);   // AetherSDR, 394: to the right edge of "Search, Hz" (70 + 4 + 8 + 4 + 70)
    leCall->setMaxLength(12);
    leCall->setObjectName("MyCall");
    leCall->setValidator(new FtLatinValidator("[A-Za-z0-9/]{0,12}", leCall));   // AetherSDR, 392
    g->addWidget(leCall, 0, 1);
    g->addWidget(new QLabel("My locator:"), 0, 2);
    leGrid = new QLineEdit;
    leGrid->setFixedWidth(200);   // AetherSDR: one width for the fields of Settings
    leGrid->setMaxLength(6);
    leGrid->setObjectName("MyLocator");
    leGrid->setValidator(new FtLatinValidator("[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2})?", leGrid));   // AetherSDR, 392
    g->addWidget(leGrid, 0, 3);
    g->addWidget(new QLabel("Decoding:"), 1, 0);
    cbDepth = new QComboBox;
    cbDepth->addItems(QStringList() << "Fast" << "Normal" << "Deep");
    cbDepth->setFixedWidth(156);   // AetherSDR, 394: as My call
    g->addWidget(cbDepth, 1, 1);
    chbAp = new QCheckBox("AP decoding");
    chbAp->setToolTip("A priori decoding (as WSJT-X): weaker signals with the known parts of the\n"
                      "messages (CQ, my call); needs my call. The decodes: the column AP (a1 .. a6).\n"
                      "FT4: with Decoding Normal or Deep. Not in WSPR.");
    g->addWidget(chbAp, 1, 2, 1, 2);
    g->addWidget(new QLabel("Search, Hz:"), 2, 0);
    QHBoxLayout *hr = new QHBoxLayout;
    sbFa = new QSpinBox;
    sbFa->setRange(100, 3000);
    sbFa->setSingleStep(50);
    sbFb = new QSpinBox;
    sbFb->setRange(500, 4000);
    sbFb->setSingleStep(50);
    sbFa->setFixedWidth(70);
    sbFb->setFixedWidth(70);
    hr->setSpacing(4);                 // AetherSDR, 394: a known width of the pair (156)
    QLabel *dashR = new QLabel("-");
    dashR->setFixedWidth(8);
    dashR->setAlignment(Qt::AlignCenter);
    hr->addWidget(sbFa);
    hr->addWidget(dashR);
    hr->addWidget(sbFb);
    hr->addStretch(1);
    g->addLayout(hr, 2, 1);
    g->addWidget(new QLabel("Time corr., ms:"), 2, 2);
    sbOffset = new QSpinBox;
    sbOffset->setFixedWidth(200);   // AetherSDR: one width for the fields of Settings
    sbOffset->setRange(-10000, 10000);
    sbOffset->setSingleStep(10);
    sbOffset->setToolTip("Added to the time of the received samples: the delay of the device and of\n"
                         "the program; set it so that DT of the stations is about 0");
    g->addWidget(sbOffset, 2, 3);
    chbPsk = new QCheckBox("PSK Reporter");
    chbPsk->setToolTip("Spots of the stations heard to pskreporter.info (FT8, FT4, WSPR; needs my call and locator)");
    g->addWidget(chbPsk, 3, 0, 1, 2);
    g->addWidget(new QLabel("Antenna:"), 3, 2);
    leAnt = new QLineEdit;
    leAnt->setFixedWidth(200);   // AetherSDR: one width for the fields of Settings
    leAnt->setMaxLength(60);
    g->addWidget(leAnt, 3, 3);
    chbAutoT = new QCheckBox("Auto time correction");
    chbAutoT->setToolTip("The time correction found by the program itself: the DT of the stations to 0;\n"
                         "with no decodes it searches +-6 s (a PC clock wrong by seconds).\n"
                         "Added to Time corr. Better: the PC clock synchronized (NTP).");
    g->addWidget(chbAutoT, 4, 0, 1, 2);
    g->addWidget(new QLabel("Rig:"), 4, 2);
    leRig = new QLineEdit;
    leRig->setFixedWidth(200);   // AetherSDR: one width for the fields of Settings
    leRig->setMaxLength(60);
    leRig->setToolTip("The receiver for PSK Reporter (the station information of the spots)");
    g->addWidget(leRig, 4, 3);
    chbEarly = new QCheckBox("Early decode (FT8)");
    chbEarly->setToolTip("As WSJT-X: a first decode at 11.8 s of the period - most of the stations are shown\n"
                         "about 2 s before the period ends; the full decode at 14.4 s adds the weak ones.\n"
                         "With Decoding Normal or Deep.");
    g->addWidget(chbEarly, 5, 0, 1, 2);
    chbMiles = new QCheckBox("Distances in miles");
    chbMiles->setToolTip("The distances of the stations in miles (off: km)");
    g->addWidget(chbMiles, 5, 2, 1, 2);
    pbWf = new QCheckBox("Waterfall, Hz:");
    pbWf->setToolTip("The waterfall of the mode at the bottom of the panorama: the left mouse button sets\n"
                     "the RX frequency (green), the right one the TX frequency (red); its height: drag the scale.\n"
                     "The range shown: from - to, Hz");
    g->addWidget(pbWf, 6, 0);
    QHBoxLayout *hw = new QHBoxLayout;
    sbWf0 = new QSpinBox;
    sbWf0->setRange(0, 3000);
    sbWf0->setSingleStep(100);
    sbWf1 = new QSpinBox;
    sbWf1->setRange(1000, 6000);
    sbWf1->setSingleStep(100);
    sbWf0->setFixedWidth(70);
    sbWf1->setFixedWidth(70);
    hw->setSpacing(4);                 // AetherSDR, 394: as the Search pair
    QLabel *dashW = new QLabel("-");
    dashW->setFixedWidth(8);
    dashW->setAlignment(Qt::AlignCenter);
    hw->addWidget(sbWf0);
    hw->addWidget(dashW);
    hw->addWidget(sbWf1);
    hw->addStretch(1);
    g->addLayout(hw, 6, 1);
    QLabel *lw = new QLabel("(the band of the decoder: 100 - 4000 Hz)");
    g->addWidget(lw, 6, 2, 1, 2);
    addTxSettings(g, 7);
    chbWsprNet = new QCheckBox("wsprnet.org");
    chbWsprNet->setToolTip("WSPR: the spots to wsprnet.org (as WSJT-X; a period with none: the station alive)");
    g->addWidget(chbWsprNet, 9, 0, 1, 2);
    g->addWidget(new QLabel("QRZ.com API key:"), 9, 2);
    leQrzKey = new QLineEdit;
    leQrzKey->setFixedWidth(200);   // AetherSDR: one width for the fields of Settings
    leQrzKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    leQrzKey->setToolTip("The key of the QRZ.com logbook: QRZ.com -> My Logbook -> Settings -> API key\n"
                         "(QRZ.com takes the QSOs with this key, not with the login and password)");
    g->addWidget(leQrzKey, 9, 3);
    lbPsk = new QLabel("PSK Reporter: off");
    g->addWidget(lbPsk, 10, 0, 1, 4);
    g->setColumnStretch(1, 1);
    g->setColumnStretch(3, 1);
    v->addWidget(gbSet);
    gbSet->setVisible(false);
    connect(pbSettings, &QPushButton::toggled, gbSet, &QWidget::setVisible);
    for(QSpinBox *sw : { sbWf0, sbWf1 })
        connect(sw, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { wfSettings(); });
    connect(psk, &PskReporter::status, lbPsk, &QLabel::setText);
    wnet = new WsprNet(this);
    qrz = new QrzLog(this);
    connect(wnet, &WsprNet::status, lbPsk, &QLabel::setText);
    connect(qrz, &QrzLog::status, lbStat, &QLabel::setText);
    connect(qrz, &QrzLog::finished, this, [this]() { qrzUi(); });
    connect(pbQrz, &QPushButton::clicked, this, [this]() {
        pbQrz->setEnabled(false);
        qrz->upload(leQrzKey->text());
    });
    for(QLineEdit *e : { leCall, leGrid, leAnt, leRig })
        connect(e, &QLineEdit::editingFinished, this, &FtPanel::applySettings);
    for(QLineEdit *e : { leCall, leGrid })
        connect(e, &QLineEdit::editingFinished, this, [this]() { genMessages(); });
    connect(leCall, &QLineEdit::textEdited, this, [this](const QString &t) {
        const int c = leCall->cursorPosition();
        leCall->setText(t.toUpper());
        leCall->setCursorPosition(c);
    });
    connect(cbDepth, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &FtPanel::applySettings);
    connect(chbAp, &QCheckBox::toggled, this, &FtPanel::applySettings);
    connect(chbPsk, &QCheckBox::toggled, this, &FtPanel::applySettings);
    connect(chbAutoT, &QCheckBox::toggled, this, &FtPanel::applySettings);
    connect(chbEarly, &QCheckBox::toggled, this, &FtPanel::applySettings);
    connect(chbMiles, &QCheckBox::toggled, this, [this]() { distUnitsUi(); });
    for(QSpinBox *s : { sbFa, sbFb, sbOffset })
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, &FtPanel::applySettings);

    // band activity (the stations heard are not shown: kept for the PSK Reporter spots)
    twAct = new QTableWidget(0, 9);
    twAct->setHorizontalHeaderLabels(QStringList() << "UTC" << "dB" << "DT" << "Hz" << "Message" << "AP" << "Country" << "km" << QString::fromUtf8("Az°"));
    twAct->horizontalHeaderItem(5)->setToolTip("A priori decoding (AP), as WSJT-X:\n"
        "a1  CQ ? ?\na2  MyCall ? ?\na3  MyCall DxCall ?\na4  MyCall DxCall RRR\n"
        "a5  MyCall DxCall 73\na6  MyCall DxCall RR73\n'?' - a doubtful AP decode");
    v->addWidget(twAct, 1);
    for(QTableWidget *t : { twAct })
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
        t->setAlternatingRowColors(false);   // AetherSDR, build 389: a background means something (as JTDX)
        t->setWordWrap(false);
        t->horizontalHeader()->setHighlightSections(false);
        QFont f("Consolas");
        f.setStyleHint(QFont::Monospace);
        f.setPointSize(9);
        t->setFont(f);
        QPalette pal = t->palette();             // light text on the dark table in any case
        pal.setColor(QPalette::Text, QColor(230, 230, 230));
        pal.setColor(QPalette::Base, QColor(22, 22, 22));
        pal.setColor(QPalette::AlternateBase, QColor(30, 30, 30));
        t->setPalette(pal);
    }
    // the table always as wide as the window: a border dragged moves with the mouse, the width
    // comes from the column next to it (left of Message: from Message; Message and right of it: from
    // the next one on the right), a new width of the window: the Message column (fitColumns)
    const int wAct[9] = { 74, 42, 48, 58, 196, 36, 150, 56, 44 };   // AetherSDR: this program's font (4-digit Hz was cut)
    for(int i = 0; i < 9; i++)
        twAct->setColumnWidth(i, wAct[i]);
    twAct->horizontalHeader()->setStretchLastSection(false);
    twAct->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    twAct->horizontalHeader()->setMinimumSectionSize(24);
    twAct->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    twAct->viewport()->installEventFilter(this);
    connect(twAct->horizontalHeader(), &QHeaderView::sectionResized, this, &FtPanel::onColumnResized);
    connect(twAct, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { callStation(row); });
    connect(twAct, &QTableWidget::cellClicked, this, [this](int row, int) {
        QTableWidgetItem *it = twAct->item(row, 3);
        if(it && !it->text().trimmed().isEmpty())
        {
            rxFreq = it->text().trimmed().toInt();
            applySettings();
            wfSettings();
        }
    });
    connect(pbClear, &QPushButton::clicked, this, [this]() {
        twAct->setRowCount(0);
        dtHist.clear();
    });

    tick = new QTimer(this);
    tick->setInterval(200);
    connect(tick, &QTimer::timeout, this, &FtPanel::onTick);
    connect(unit, &FtUnit::decoded, this, [this](qint64 p, int m, QVector<FtUnit::Decode> l, int ms) {
        onDecoded(p, m, l, ms, false); }, Qt::QueuedConnection);
    connect(unit, &FtUnit::earlyDecoded, this, [this](qint64 p, int m, QVector<FtUnit::Decode> l, int ms) {
        onDecoded(p, m, l, ms, true); }, Qt::QueuedConnection);
    // the waterfall: a strip at the bottom of the panorama of the main window (it takes it:
    // waterfall(), WdgGraph::setBottomWidget), shown while FTX is on and the button is on
    wf = new FtWaterfall(unit, 0);
    wf->hide();
    connect(wf, &FtWaterfall::rxClicked, this, [this](int hz) { rxFreq = hz; applySettings(); wfSettings(); });
    connect(wf, &FtWaterfall::txClicked, this, [this](int hz) { txFreq = hz; wfSettings(); });
    connect(wf, &FtWaterfall::heightRequested, this, [this](int h) { wfHeight = qBound(60, h, 2000); });
    connect(pbWf, &QCheckBox::toggled, this, [this](bool on) {
        if(on && isVisible())
            showWaterfall();
        else if(!on && wf)
            wf->hide();
    });
    fillBands();
}

FtPanel::~FtPanel()
{
    delete wf;                          // (gone already when the main window took it)
}

// AetherSDR, build 409
bool FtPanel::waterfallOn() const
{
    return pbWf->isChecked();
}

void FtPanel::waterfallRange(int &fromHz, int &toHz) const
{
    fromHz = sbWf0->value();
    toHz = qMax(sbWf0->value() + 500, sbWf1->value());
}

// the waterfall strip shown (in the main window)
void FtPanel::showWaterfall()
{
    if(!wf || !wf->parentWidget())
        return;                         // not in the main window (yet)
    wfSettings();
    wf->setRunning(running);
    wf->show();
}

void FtPanel::wfSettings()
{
    lbRx->setText(QString("RX %1").arg(rxFreq));
    lbTx->setText(QString("TX %1").arg(txFreq));
    if(!wf)
        return;
    wf->setRange(sbWf0->value(), qMax(sbWf0->value() + 500, sbWf1->value()));
    wf->setSearch(sbFa->value(), qMax(sbFa->value() + 200, sbFb->value()));
    wf->setMode(curMode);
    wf->setRx(rxFreq);
    wf->setTx(txFreq);
}

// ---- settings ------------------------------------------------------------------------------------

void FtPanel::loadSettings()
{
    QSettings st(QDir::homePath() + "/AetherSDR-FTX/Options.ini", QSettings::IniFormat);
    st.beginGroup("FT");
    const int m = st.value("Mode", FtUnit::FT8).toInt();
    curMode = (m == FtUnit::FT4 || m == FtUnit::WSPR) ? m : FtUnit::FT8;    // (FT2 of old: FT8)
    for(int i = 0; i < 3; i++)
    {
        bandOf[i] = qBound(0, st.value(QString("Band%1").arg(modeKey(i)), 5).toInt(), BANDS - 1);
        for(int b = 0; b < BANDS; b++)
            freqs[i][b] = st.value(QString("Freq%1_%2").arg(modeKey(i)).arg(b), defFreq[i][b]).toLongLong();
    }
    for(int b = 0; b < BANDS; b++)
        st.remove(QString("Freq2_%1").arg(b));          // the FT2 frequencies of old
    st.remove("Band2");
    winGeom = st.value("WindowGeometry").toByteArray();
    {
        // the column widths set by hand (the message column: the width left)
        const QByteArray hs = st.value("ActColumns3").toByteArray();
        if(!hs.isEmpty())
        {
            colFitting = true;
            twAct->horizontalHeader()->restoreState(hs);
            twAct->horizontalHeader()->setStretchLastSection(false);
            twAct->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
            colFitting = false;
        }
        fitColumns();
    }
    leCall->setText(st.value("MyCall", "").toString());
    leGrid->setText(st.value("MyGrid", "").toString());
    leAnt->setText(st.value("Antenna", "").toString());
    // AetherSDR (build 383): the rig named in the spots is this program; the
    // default MicronSDR had saved is replaced once.
    {
        // (build 396: "AetherSDR N7DDC"; the earlier default replaced once)
        // (build 402: the fork's name; the earlier defaults replaced once)
        const QString forkRig = AetherSDR::forkDisplayName();
        QString rig = st.value("Rig", forkRig).toString();
        if(rig.isEmpty() || rig == "SDR Micron (N7DDC)" || rig == "AetherSDR" || rig == "AetherSDR N7DDC")
            rig = forkRig;
        leRig->setText(rig);
    }
    cbDepth->setCurrentIndex(qBound(0, st.value("Depth", 2).toInt(), 2));
    chbAp->setChecked(st.value("AP", true).toBool());
    sbFa->setValue(st.value("Fa", 200).toInt());
    sbFb->setValue(st.value("Fb", 3500).toInt());
    sbOffset->setValue(st.value("TimeOffsetMs", 0).toInt());
    chbPsk->setChecked(st.value("PskReporter", false).toBool());
    chbWsprNet->setChecked(st.value("WsprNet", false).toBool());
    leQrzKey->setText(st.value("QrzApiKey", "").toString());
    chbAutoT->setChecked(st.value("AutoTime", true).toBool());
    chbEarly->setChecked(st.value("EarlyDecode", true).toBool());
    chbMiles->setChecked(st.value("Miles", false).toBool());
    // the correction with decodes (AutoOffsetMs of old could be a search step: not taken)
    autoSaved = st.value("AutoOffsetGoodMs", 0).toInt();
    st.remove("AutoOffsetMs");
    rxFreq = st.value("RxFreq", 1500).toInt();
    txFreq = st.value("TxFreq", 1500).toInt();
    pbSettings->setChecked(st.value("SettingsOpen", false).toBool());
    loadTxSettings(st);
    st.remove("WaterfallGeometry");
    wfHeight = qBound(60, st.value("WaterfallHeight", 170).toInt(), 2000);
    sbWf0->setValue(st.value("WaterfallFrom", 0).toInt());
    sbWf1->setValue(st.value("WaterfallTo", 4000).toInt());
    pbWf->blockSignals(true);
    pbWf->setChecked(st.value("WaterfallShown", true).toBool());
    pbWf->blockSignals(false);
    st.endGroup();
    bgMode->button(curMode)->setChecked(true);
    fillBands();
    modeUi();
    distUnitsUi();
    wfSettings();
}

QString FtPanel::dist(double km) const
{
    if(km < 0)
        return QString();
    return QString::number(qRound(chbMiles->isChecked() ? km * 0.621371 : km));
}

// km / miles: the column headers and the distances shown (the km kept in the items)
void FtPanel::distUnitsUi()
{
    const QString u = chbMiles->isChecked() ? "mi" : "km";
    twAct->horizontalHeaderItem(7)->setText(u);
    for(int r = 0; r < twAct->rowCount(); r++)
    {
        QTableWidgetItem *it = twAct->item(r, 7);
        if(it && it->data(Qt::UserRole).isValid())
            it->setText(dist(it->data(Qt::UserRole).toDouble()));
    }
}

void FtPanel::saveSettings()
{
    QSettings st(QDir::homePath() + "/AetherSDR-FTX/Options.ini", QSettings::IniFormat);
    st.beginGroup("FT");
    st.setValue("Mode", curMode);
    for(int i = 0; i < 3; i++)
    {
        st.setValue(QString("Band%1").arg(modeKey(i)), bandOf[i]);
        for(int b = 0; b < BANDS; b++)
        {
            if(freqs[i][b] != defFreq[i][b])
                st.setValue(QString("Freq%1_%2").arg(modeKey(i)).arg(b), freqs[i][b]);
            else
                st.remove(QString("Freq%1_%2").arg(modeKey(i)).arg(b));
        }
    }
    st.setValue("MyCall", leCall->text().trimmed().toUpper());
    st.setValue("MyGrid", leGrid->text().trimmed());
    st.setValue("Antenna", leAnt->text().trimmed());
    st.setValue("Rig", leRig->text().trimmed());
    st.setValue("Depth", cbDepth->currentIndex());
    st.setValue("AP", chbAp->isChecked());
    st.setValue("Fa", sbFa->value());
    st.setValue("Fb", sbFb->value());
    st.setValue("TimeOffsetMs", sbOffset->value());
    st.setValue("PskReporter", chbPsk->isChecked());
    st.setValue("WsprNet", chbWsprNet->isChecked());
    st.setValue("QrzApiKey", leQrzKey->text().trimmed());
    st.setValue("AutoTime", chbAutoT->isChecked());
    st.setValue("EarlyDecode", chbEarly->isChecked());
    st.setValue("Miles", chbMiles->isChecked());
    st.setValue("AutoOffsetGoodMs", autoSaved);
    st.setValue("RxFreq", rxFreq);
    st.setValue("TxFreq", txFreq);
    st.setValue("SettingsOpen", pbSettings->isChecked());
    saveTxSettings(st);
    st.setValue("WaterfallHeight", wfHeight);
    st.setValue("WaterfallFrom", sbWf0->value());
    st.setValue("WaterfallTo", sbWf1->value());
    st.setValue("WaterfallShown", pbWf->isChecked());
    if(!winGeom.isEmpty())
        st.setValue("WindowGeometry", winGeom);
    st.setValue("ActColumns3", twAct->horizontalHeader()->saveState());
    st.endGroup();
}

void FtPanel::applySettings()
{
    if(!running)
        return;                         // the unit belongs to the window that runs (FT or JS8)
    FtUnit::Settings s;
    s.mode = curMode;
    s.depth = cbDepth->currentIndex() + 1;
    s.ap = chbAp->isChecked();
    s.nfa = sbFa->value();
    s.nfb = qMax(sbFa->value() + 200, sbFb->value());
    s.nfqso = rxFreq;
    s.mycall = leCall->text().trimmed().toUpper();
    s.timeOffsetMs = sbOffset->value();
    s.autoTime = chbAutoT->isChecked();
    s.early = chbEarly->isChecked();
    s.dialHz = dial > 0 ? dial : freqs[modeIndex(curMode)][bandOf[modeIndex(curMode)]];
    unit->setSettings(s);
    wfSettings();
    QString grid = leGrid->text().trimmed();
    if(grid.size() >= 4)
        grid = grid.left(2).toUpper() + grid.mid(2, 2) + grid.mid(4).toLower();
    psk->setLocalStation(s.mycall, grid, leAnt->text(), leRig->text());
    psk->setEnabled(running && chbPsk->isChecked());     // (WSPR too)
    if(chbPsk->isChecked() && (s.mycall.isEmpty() || grid.size() < 4))
        lbPsk->setText("PSK Reporter: my call and locator are needed");
}

// WSPR: AP and the search range not used (wsprd: +-150 Hz around 1500 Hz); the column AP = the drift
void FtPanel::modeUi()
{
    const bool w = (curMode == FtUnit::WSPR);
    chbAp->setEnabled(!w);
    chbEarly->setEnabled(curMode == FtUnit::FT8);
    sbFa->setEnabled(!w);
    sbFb->setEnabled(!w);
    twAct->horizontalHeaderItem(5)->setText(w ? "Dr" : "AP");
    twAct->horizontalHeaderItem(5)->setToolTip(w ? QString("WSPR: the drift of the signal, Hz in 2 minutes")
        : QString("A priori decoding (AP), as WSJT-X:\n"
                  "a1  CQ ? ?\na2  MyCall ? ?\na3  MyCall DxCall ?\na4  MyCall DxCall RRR\n"
                  "a5  MyCall DxCall 73\na6  MyCall DxCall RR73\n'?' - a doubtful AP decode"));
    if(w && unit->error().isEmpty() && !unit->hasWspr() && running)
        lbStat->setText("libftx.dll is old (no WSPR): build FT_src again (build.sh)");
}

// ---- mode, band, frequency -------------------------------------------------------------------------

void FtPanel::fillBands()
{
    const int mi = modeIndex(curMode);
    cbBand->blockSignals(true);
    cbBand->clear();
    for(int b = 0; b < BANDS; b++)
        if(freqs[mi][b] > 0 && (b < HF_BANDS || (bandReach && bandReach(freqs[mi][b]))))
            cbBand->addItem(bandName[b], b);   // AetherSDR, 390: above the radio -- with a transverter only
    int idx = cbBand->findData(bandOf[mi]);
    if(idx < 0)
        idx = cbBand->findData(5);
    cbBand->setCurrentIndex(qMax(0, idx));
    // (AetherSDR, 390: a transverter band kept from the last time stays until setBandReach() has
    // said whether its transverter is still there)
    if(bandReach || bandOf[mi] < HF_BANDS)
        bandOf[mi] = cbBand->currentData().toInt();
    cbBand->blockSignals(false);
    cbFreq->blockSignals(true);
    cbFreq->clear();
    const qint64 f = freqs[mi][bandOf[mi]];
    cbFreq->addItem(QString::number(f / 1000.0, 'f', 3));
    const qint64 df = defFreq[mi][bandOf[mi]];
    if(f != df && df > 0)
        cbFreq->addItem(QString::number(df / 1000.0, 'f', 3) + " (default)");
    for(const auto &a : ALT_FREQ)
        if(a.mode == mi && a.band == bandOf[mi] && a.hz != f && a.hz != df)
            cbFreq->addItem(QString::number(a.hz / 1000.0, 'f', 3) + (a.note[0] ? QString(" (%1)").arg(a.note) : QString()));
    cbFreq->setCurrentIndex(0);
    cbFreq->blockSignals(false);
}

void FtPanel::setMode(int m)
{
    if(m != FtUnit::FT8 && m != FtUnit::FT4 && m != FtUnit::WSPR)
        return;
    const bool changed = (m != curMode);
    if(changed && (txOn || pbEnable->isChecked()))
        haltTx();                       // the transmitter of the old mode off
    curMode = m;
    if(bgMode->button(m) && !bgMode->button(m)->isChecked())
        bgMode->button(m)->setChecked(true);
    if(!changed)
        return;
    fillBands();
    applySettings();
    wfSettings();
    modeUi();
    lastPeriodShown = 0;
    dtHist.clear();                     // the DT of another mode
    genMessages();
    txUi();
    if(running)
        tuneBand();
    emit modeChanged(curMode);
}

void FtPanel::onBand(int index)
{
    const int mi = modeIndex(curMode);
    bandOf[mi] = cbBand->itemData(index).toInt();
    fillBands();
    if(running)
        tuneBand();
}

void FtPanel::onFreqEdited()
{
    QString t = cbFreq->currentText();
    t = t.left(t.indexOf(' ') < 0 ? t.size() : t.indexOf(' ')).trimmed();
    t.replace(',', '.');
    bool ok = false;
    const double khz = t.toDouble(&ok);
    const int mi = modeIndex(curMode);
    if(ok && khz >= 1000.0 && khz <= 2000000.0)   // AetherSDR, 390: was 35 MHz (6 m and transverters refused)
    {
        const qint64 hz = qRound64(khz * 1000.0);
        if(hz != freqs[mi][bandOf[mi]])
        {
            freqs[mi][bandOf[mi]] = hz;
            fillBands();
            if(running)
                tuneBand();
        }
    }
    else
        fillBands();
}

void FtPanel::tuneBand()
{
    const int mi = modeIndex(curMode);
    const qint64 f = freqs[mi][bandOf[mi]];
    if(txOn || pbEnable->isChecked())
        haltTx();                       // another band: the transmission off
    if(f > 0)
        emit tuneRequest(f);
}

void FtPanel::setDialShown(qint64 hz)
{
    if(hz == dial)
        return;
    dial = hz;
    applySettings();                    // WSPR: the frequencies of the decodes
}

void FtPanel::showWindow(const QRect &mainGeom)
{
    if(!winGeom.isEmpty() && restoreGeometry(winGeom))
    {
        // still on a screen (a monitor gone: next to the main window again)
        if(!QGuiApplication::screenAt(geometry().center()))
            winGeom.clear();
    }
    if(winGeom.isEmpty())
    {
        // the first time: left of the main window if there is room, else over its left part
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
    if(pbWf->isChecked())
        showWaterfall();
}

void FtPanel::hideWindow()
{
    if(isVisible())
        winGeom = saveGeometry();
    if(wf)
        wf->hide();                     // (the button keeps its state: shown again with the FTX window)
    hide();
    saveSettings();
}

void FtPanel::closeEvent(QCloseEvent *e)
{
    winGeom = saveGeometry();
    e->accept();
    emit closed();                      // -> FTX off (hideWindow)
}

void FtPanel::start()
{
    if(!ctyLoaded)
        loadCty();
    running = true;
    unit->setAutoOffset(autoSaved);     // the JS8 window has its own
    applySettings();
    unit->setActive(true);
    if(wf)
        wf->setRunning(true);
    wfSettings();
    tuneBand();
    tick->start();
    txTick->start();
    lastAction = QDateTime::currentMSecsSinceEpoch();
    lbStat->setText(unit->error().isEmpty() ? QString("Waiting for the first period") : unit->error());
    modeUi();
}

void FtPanel::stop()
{
    if(running)
        autoSaved = unit->autoGoodMs();       // the time correction found in this run (with decodes, not a search step)
    running = false;
    if(txOn)
        finishTx();                     // (MOX off)
    pbEnable->setChecked(false);
    txTick->stop();
    unit->setActive(false);
    if(wf)
        wf->setRunning(false);
    psk->setEnabled(false);
    tick->stop();
    saveSettings();
}

void FtPanel::onTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    lbUtc->setText(QDateTime::fromMSecsSinceEpoch(now, Qt::UTC).toString("HH:mm:ss"));
    const int P = FtUnit::periodMs(curMode);
    const int corr = sbOffset->value() + (chbAutoT->isChecked() ? unit->autoOffsetMs() : 0);
    pbPeriod->setValue((int)(((now + corr) % P + P) % P * 1000 / P));
    QString lv = unit->levelDb() > -199.0f
                 ? QString("In %1 dBFS, pk %2").arg(unit->levelDb(), 0, 'f', 0).arg(unit->peakDb(), 0, 'f', 0)
                 : QString("In --");
    if(chbAutoT->isChecked())
        lv += unit->autoSearching() ? QString("  t %1 s?").arg(unit->autoOffsetMs() / 1000.0, 0, 'f', 1)
                                    : QString("  t %1%2 s").arg(unit->autoOffsetMs() >= 0 ? "+" : "").arg(unit->autoOffsetMs() / 1000.0, 0, 'f', 2);
    lbLevel->setText(lv);
}

// ---- decodes ---------------------------------------------------------------------------------------

bool FtPanel::isCall(const QString &w)
{
    if(w.size() < 3 || w.size() > 12)
        return false;
    bool dig = false, let = false;
    for(QChar c : w)
    {
        if(c.isDigit())
            dig = true;
        else if(c >= 'A' && c <= 'Z')
            let = true;
        else if(c != '/')
            return false;
    }
    return dig && let;
}

bool FtPanel::isGrid4(const QString &w)
{
    return w.size() == 4 && w != "RR73" && w[0] >= 'A' && w[0] <= 'R' && w[1] >= 'A' && w[1] <= 'R'
           && w[2].isDigit() && w[3].isDigit();
}

bool FtPanel::isGrid6(const QString &w)
{
    return w.size() == 6 && isGrid4(w.left(4)) && w[4].toUpper() >= 'A' && w[4].toUpper() <= 'X'
           && w[5].toUpper() >= 'A' && w[5].toUpper() <= 'X';
}

bool FtPanel::gridToLatLon(const QString &gIn, double &lat, double &lon)
{
    const QString g = gIn.toUpper();
    if(g.size() < 4 || g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R' || !g[2].isDigit() || !g[3].isDigit())
        return false;
    lon = (g[0].unicode() - 'A') * 20.0 - 180.0 + (g[2].unicode() - '0') * 2.0;
    lat = (g[1].unicode() - 'A') * 10.0 - 90.0 + (g[3].unicode() - '0') * 1.0;
    if(g.size() >= 6 && g[4] >= 'A' && g[4] <= 'X' && g[5] >= 'A' && g[5] <= 'X')
    {
        lon += (g[4].unicode() - 'A') * (2.0 / 24.0) + 1.0 / 24.0;
        lat += (g[5].unicode() - 'A') * (1.0 / 24.0) + 0.5 / 24.0;
    }
    else
    {
        lon += 1.0;
        lat += 0.5;
    }
    return true;
}

void FtPanel::distAz(double lat1, double lon1, double lat2, double lon2, double &km, double &az)
{
    const double d2r = M_PI / 180.0;
    const double p1 = lat1 * d2r, p2 = lat2 * d2r, dl = (lon2 - lon1) * d2r;
    const double c = sin(p1) * sin(p2) + cos(p1) * cos(p2) * cos(dl);
    km = 6371.0 * acos(qBound(-1.0, c, 1.0));
    az = atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) / d2r;
    if(az < 0)
        az += 360.0;
}

void FtPanel::onDecoded(qint64 periodStartMs, int mode, QVector<FtUnit::Decode> list, int decodeMs, bool early)
{
    if(!running || FtUnit::isJs8(mode))
        return;
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(periodStartMs, Qt::UTC);
    if(decodeMs < 0)
    {
        if(mode == FtUnit::WSPR)
            lbStat->setText(QString("WSPR %1: begun in the middle (not decoded), the next period: %2 UTC")
                            .arg(t.toString("HH:mm")).arg(t.addSecs(120).toString("HH:mm")));
        else
            lbStat->setText(QString("%1 %2: not enough audio, waiting").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss")));
        return;
    }
    if(txPeriodsMs.contains(periodStartMs))
    {
        // my transmission: not decoded (no own signal in the band activity, no spots of it)
        lbStat->setText(QString("%1 %2: my transmission").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss")));
        return;
    }
    const bool wspr = (mode == FtUnit::WSPR);
    const QString utc = wspr ? t.toString("HHmm") : t.toString(mode == FtUnit::FT8 ? "HHmmss" : "HHmmss.z").left(mode == FtUnit::FT8 ? 6 : 8);
    if(msgsPeriod != periodStartMs)
    {
        msgsPeriod = periodStartMs;
        shownMsgs.clear();
        earlyCount = 0;
    }
    // the same message twice in a period (early + full, the a7 decodes): once
    for(int i = list.size() - 1; i >= 0; i--)
    {
        const QString k = list[i].msg.simplified();
        if(shownMsgs.contains(k))
            list.remove(i);
        else
            shownMsgs.insert(k);
    }
    if(early)
    {
        earlyCount = list.size();
        lbStat->setText(QString("%1 %2: %3 decodes (early), %4 ms").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss"))
                        .arg(list.size()).arg(decodeMs));
    }
    else if(earlyCount > 0)
        lbStat->setText(QString("%1 %2: %3 decodes (early %4), %5 ms").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss"))
                        .arg(list.size() + earlyCount).arg(earlyCount).arg(decodeMs));
    else
        lbStat->setText(QString("%1 %2: %3 decodes, %4 ms").arg(FtUnit::modeName(mode)).arg(t.toString("HH:mm:ss"))
                        .arg(list.size()).arg(decodeMs));
    const QString myCall = leCall->text().trimmed().toUpper();
    double myLat = 0, myLon = 0;
    const bool haveMy = gridToLatLon(leGrid->text().trimmed(), myLat, myLon);
    // AetherSDR, build 389: the highlighting by the rules of JTDX (displaytext.cpp)
    if(!wkLoaded)
        loadWorked();
    const QString dxCall = leDx->text().trimmed().toUpper();
    const QString bandNow = FtLog::band(dial > 0 ? dial : freqs[modeIndex(curMode)][bandOf[modeIndex(curMode)]]);

    QScrollBar *sb = twAct->verticalScrollBar();
    const bool atEnd = sb->value() >= sb->maximum() - 2;
    if(!list.isEmpty() && twAct->rowCount() > 0 && lastPeriodShown != periodStartMs)
    {
        // a thin line between the periods
        const int r = twAct->rowCount();
        twAct->insertRow(r);
        twAct->setRowHeight(r, 3);
        for(int c = 0; c < twAct->columnCount(); c++)
        {
            QTableWidgetItem *it = new QTableWidgetItem;
            it->setBackground(QColor(0x4a, 0x68, 0x80));   // AetherSDR, 392: the applet panel's scroll bar colour (was grey)
            it->setFlags(Qt::NoItemFlags);
            twAct->setItem(r, c, it);
        }
    }
    if(!list.isEmpty())
        lastPeriodShown = periodStartMs;    // (an empty early decode: the line before the full one still)

    // sorted by the audio frequency, as WSJT-X
    std::sort(list.begin(), list.end(), [](const FtUnit::Decode &a, const FtUnit::Decode &b) { return a.freq < b.freq; });
    for(const FtUnit::Decode &d : list)
    {
        if(d.nap && !chbAp->isChecked())
            continue;               // AP off (the FT4 decoder tries AP always)
        dtHist.append(d.dt);
        if(dtHist.size() > 60)
            dtHist.removeFirst();
        const QStringList w = d.msg.split(' ', Qt::SkipEmptyParts);
        QString call, grid;
        bool cq = false;
        if(wspr)
        {
            // "K1ABC FN42 37", "<PJ4/K1ABC> FK52UD 37", "PJ4/K1ABC 37"
            if(!w.isEmpty())
                call = w[0];
            if(w.size() >= 3 && (isGrid4(w[1]) || isGrid6(w[1])))
                grid = w[1].left(2) + w[1].mid(2, 2) + w[1].mid(4).toLower();
        }
        else if(!w.isEmpty() && (w[0] == "CQ" || w[0] == "QRZ" || w[0] == "DE"))
        {
            cq = (w[0] != "DE");
            int i = 1;
            if(w.size() > 2 && !isCall(w[1]))
                i = 2;
            if(i < w.size())
                call = w[i];
            if(i + 1 < w.size() && isGrid4(w[i + 1]))
                grid = w[i + 1];
        }
        else if(w.size() >= 2)
        {
            call = w[1];
            if(w.size() >= 3 && isGrid4(w[2]))
                grid = w[2];
        }
        call.remove('<').remove('>');
        if(!isCall(call))
            call.clear();
        QString country, cont;
        double lat = 0, lon = 0;
        bool havePos = false;
        if(!call.isEmpty())
        {
            const int e = entityOf(call);
            if(e >= 0)
            {
                country = ents[e].name;
                cont = ents[e].cont;
                lat = ents[e].lat;
                lon = ents[e].lon;
                havePos = true;
            }
        }
        if(!grid.isEmpty())
            havePos = gridToLatLon(grid, lat, lon) || havePos;
        double km = -1, az = -1;
        if(havePos && haveMy)
            distAz(myLat, myLon, lat, lon, km, az);

        const int r = twAct->rowCount();
        twAct->insertRow(r);
        const QStringList cells = { utc, QString::number(d.snr), QString::number(d.dt, 'f', 1),
                                    QString::number(qRound(d.freq)) + "  ", d.msg,     // (2 spaces: a gap before the message)
                                    wspr ? QString::number(d.bits)
                                         : d.nap ? QString("a%1%2").arg(d.nap).arg(d.qual < 0.17f ? "?" : "") : QString(),
                                    country, dist(km),
                                    az >= 0 ? QString::number(qRound(az)) : QString() };
        // AetherSDR, build 389: the rules of JTDX (displaytext.cpp, its defaults), the colours
        // this program's, muted.
        //   - to my call (std_type 2): its own colour, before anything else;
        //   - a station one may call -- CQ / QRZ (1) or the end of its QSO, "73" / "RR73" (4, the
        //     "73 / RR73 marker"): the background says what it would be for the log -- a new
        //     country, a new country on this band, a new call, a new call on this band; worked on
        //     this band already: no background, the message struck through;
        //   - the other messages (3): plain; those with the DX of the QSO now: another text colour.
        const QString first = w.isEmpty() ? QString() : QString(w[0]).remove('<').remove('>');
        const bool toMe = !wspr && !myCall.isEmpty() && first == myCall;
        const bool fin = !wspr && !cq && !toMe && !call.isEmpty() && w.size() >= 3
                         && (w.last() == "73" || w.last() == "RR73");
        const bool withDx = !wspr && !dxCall.isEmpty() && w.contains(dxCall);
        QColor rowBg;
        bool worked = false;
        if(toMe)
            rowBg = QColor(104, 34, 34);
        else if((cq || fin) && !call.isEmpty() && call != myCall)
        {
            const int ent = entityOf(call);
            if(ent >= 0 && !wkEnt.contains(ent))
                rowBg = QColor(84, 38, 92);         // a new country
            else if(ent >= 0 && !wkEntBand.contains(QString::number(ent) + "|" + bandNow))
                rowBg = QColor(52, 44, 96);         // a new country on this band
            else if(!wkCall.contains(call))
                rowBg = QColor(18, 58, 88);         // a new call (this program's blue)
            else if(!wkCallBand.contains(call + "|" + bandNow))
                rowBg = QColor(16, 68, 72);         // a new call on this band
            else
                worked = true;
        }
        for(int c = 0; c < cells.size(); c++)
        {
            QTableWidgetItem *it = new QTableWidgetItem(cells[c]);
            it->setForeground(worked ? QColor(140, 140, 140) : withDx && !toMe ? QColor(130, 200, 235)
                                                                              : QColor(230, 230, 230));
            if(c <= 3 || c >= 7)
                it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if(d.nap && (c == 4 || c == 5) && !worked)
                it->setForeground(QColor(255, 210, 90));    // AP decodes: yellow
            if(rowBg.isValid())
                it->setBackground(rowBg);
            if(worked && c == 4)
            {
                QFont sf = twAct->font();
                sf.setStrikeOut(true);
                it->setFont(sf);
                it->setToolTip("Worked on this band already");
            }
            if(c == 6 && !cont.isEmpty())
                it->setToolTip(country + ", " + cont);
            if(c == 7 && km >= 0)
                it->setData(Qt::UserRole, km);
            if(wspr && c == 3 && dial > 0)
                it->setToolTip(QString("%1 MHz").arg((dial + d.freq) / 1e6, 0, 'f', 6));
            if(c == 0)
                it->setData(Qt::UserRole, periodStartMs);       // (a double click: the period of the station)
            twAct->setItem(r, c, it);
        }
        if(!call.isEmpty())
        {
            addStation(call, grid, d.snr, cq, t);
            // PSK Reporter: every station heard; its locator from this message or from one
            // heard before (the messages of a QSO have none)
            if(dial > 0)
                psk->addSpot(call, grid.isEmpty() ? stations.value(call).grid : grid, dial + qRound(d.freq),
                             FtUnit::modeName(mode), d.snr, t);
        }
    }
    autoSeq(periodStartMs, list, early);     // the transmitter: the answers to me
    if(wspr)
        wsprSpots(periodStartMs, list);      // wsprnet.org
    while(twAct->rowCount() > 1500)
        twAct->removeRow(0);
    if(atEnd)
        twAct->scrollToBottom();
    if(stations.size() > 20000)
        stations.clear();               // (hours of a contest)
    if(!dtHist.isEmpty())
    {
        double s = 0;
        for(double x : dtHist)
            s += x;
        const double avg = s / dtHist.size();
        const double lim = (curMode == FtUnit::WSPR) ? 1.0 : (curMode == FtUnit::FT4 ? 0.15 : 0.3);
        lbDt->setText(QString("DT %1%2 s").arg(avg >= 0 ? "+" : "").arg(avg, 0, 'f', 2));
        lbDt->setStyleSheet(fabs(avg) > lim ? "color: #FF8080;" : "color: #90E090;");
    }
}

void FtPanel::addStation(const QString &call, const QString &grid, int snr, bool cq, const QDateTime &t)
{
    Station &s = stations[call];
    if(s.call.isEmpty())
    {
        s.call = call;
        s.bestSnr = snr;
        s.count = 0;
        s.km = -1;
        s.az = -1;
        const int e = entityOf(call);
        if(e >= 0)
        {
            s.country = ents[e].name;
            s.cont = ents[e].cont;
        }
    }
    if(!grid.isEmpty())
        s.grid = grid;
    s.lastSnr = snr;
    s.bestSnr = qMax(s.bestSnr, snr);
    s.count++;
    s.last = t;
    s.cq = cq;
    double myLat, myLon, lat, lon;
    bool pos = false;
    if(!s.grid.isEmpty())
        pos = gridToLatLon(s.grid, lat, lon);
    if(!pos)
    {
        const int e = entityOf(call);
        if(e >= 0)
        {
            lat = ents[e].lat;
            lon = ents[e].lon;
            pos = true;
        }
    }
    if(pos && gridToLatLon(leGrid->text().trimmed(), myLat, myLon))
        distAz(myLat, myLon, lat, lon, s.km, s.az);
}

// ---- cty.dat ---------------------------------------------------------------------------------------

void FtPanel::loadCty()
{
    ctyLoaded = true;
    QFile f(QCoreApplication::applicationDirPath() + "/cty.dat");
    if(!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "FT: no cty.dat next to the program (no countries)";
        return;
    }
    const QString all = QString::fromLatin1(f.readAll());
    const QStringList recs = all.split(';', Qt::SkipEmptyParts);
    static const QRegularExpression ovr("\\(.*?\\)|\\[.*?\\]|<.*?>|\\{.*?\\}|~.*?~");
    for(const QString &rec : recs)
    {
        const QStringList fl = rec.split(':');
        if(fl.size() < 9)
            continue;
        Entity e;
        e.name = fl[0].trimmed();
        e.cq = fl[1].trimmed().toInt();
        e.itu = fl[2].trimmed().toInt();
        e.cont = fl[3].trimmed();
        e.lat = fl[4].trimmed().toDouble();
        e.lon = -fl[5].trimmed().toDouble();        // cty.dat: west positive
        const int id = ents.size();
        ents.append(e);
        const QStringList ps = fl[8].split(',', Qt::SkipEmptyParts);
        for(QString p : ps)
        {
            p = p.trimmed();
            p.remove(ovr);
            p = p.trimmed().toUpper();
            if(p.isEmpty())
                continue;
            if(p.startsWith('='))
                exact.insert(p.mid(1), id);
            else if(!pfx.contains(p))
                pfx.insert(p, id);
        }
    }
    qDebug() << "FT: cty.dat:" << ents.size() << "entities," << pfx.size() << "prefixes," << exact.size() << "calls";
}

// AetherSDR, build 389: the log read once (the ADIF file of FtLog), then kept by logQso()
void FtPanel::loadWorked()
{
    wkLoaded = true;
    wkCall.clear();
    wkCallBand.clear();
    wkEnt.clear();
    wkEntBand.clear();
    for(const QString &r : FtLog::records())
        addWorked(FtLog::field(r, "call"), FtLog::field(r, "band"));
}

void FtPanel::addWorked(const QString &callIn, const QString &bandIn)
{
    const QString call = callIn.trimmed().toUpper(), band = bandIn.trimmed().toLower();
    if(call.isEmpty())
        return;
    wkCall.insert(call);
    wkCallBand.insert(call + "|" + band);
    const int ent = entityOf(call);
    if(ent >= 0)
    {
        wkEnt.insert(ent);
        wkEntBand.insert(QString::number(ent) + "|" + band);
    }
}

// AetherSDR, build 396 (operator, 14.11): a word over the middle of the table for 2 s -- the
// table's own child, so the Settings group shown or not does not move it
void FtPanel::showToast(const QString &text)
{
    if(!lbToast)
    {
        lbToast = new QLabel(twAct);
        lbToast->setAlignment(Qt::AlignCenter);
        lbToast->setAttribute(Qt::WA_TransparentForMouseEvents);
        lbToast->setStyleSheet("QLabel { background: #1b4a66; color: white; border: 1px solid #6a90b0;"
                               " border-radius: 6px; padding: 10px 18px; font-size: 14px; font-weight: bold; }");
        toastTimer = new QTimer(this);
        toastTimer->setSingleShot(true);
        connect(toastTimer, &QTimer::timeout, lbToast, &QWidget::hide);
    }
    lbToast->setText(text);
    lbToast->adjustSize();
    lbToast->move((twAct->width() - lbToast->width()) / 2, (twAct->height() - lbToast->height()) / 2);
    lbToast->show();
    lbToast->raise();
    toastTimer->start(2000);
}

int FtPanel::entityOf(const QString &callIn) const
{
    if(ents.isEmpty())
        return -1;
    const QString call = callIn.toUpper();
    auto ex = exact.find(call);
    if(ex != exact.end())
        return ex.value();
    QString base = call;
    const QStringList parts = call.split('/', Qt::SkipEmptyParts);
    if(parts.size() >= 2)
    {
        static const QStringList suffix = { "P", "M", "MM", "AM", "QRP", "A", "R", "LH", "J" };
        QString a = parts[0], b = parts[1];
        if(suffix.contains(b) || (b.size() == 1 && b[0].isDigit()))
            base = a;
        else if(suffix.contains(a))
            base = b;
        else
            base = (b.size() < a.size()) ? b : a;       // the prefix part (K1ABC/VE3, VP2E/K1ABC)
    }
    for(int n = qMin(base.size(), 12); n >= 1; n--)
    {
        auto it = pfx.find(base.left(n));
        if(it != pfx.end())
            return it.value();
    }
    return -1;
}

// ---- the columns of the band activity --------------------------------------------------------------

static const int COL_MSG = 4;

// the Message column: the width the others leave (the table as wide as its view)
void FtPanel::fitColumns()
{
    QHeaderView *h = twAct->horizontalHeader();
    const int W = twAct->viewport()->width();
    if(W <= 0)
        return;
    int others = 0;
    for(int c = 0; c < twAct->columnCount(); c++)
        if(c != COL_MSG)
            others += h->sectionSize(c);
    colFitting = true;
    h->resizeSection(COL_MSG, qMax(h->minimumSectionSize(), W - others));
    // still too wide (the others too wide for the window): the right ones narrower
    int over = h->length() - W;
    for(int c = twAct->columnCount() - 1; c > COL_MSG && over > 0; c--)
    {
        const int take = qMin(over, h->sectionSize(c) - h->minimumSectionSize());
        if(take > 0)
        {
            h->resizeSection(c, h->sectionSize(c) - take);
            over -= take;
        }
    }
    colFitting = false;
}

// a border dragged by hand: the column next to it gives / takes the width (as a splitter)
void FtPanel::onColumnResized(int col, int oldSize, int newSize)
{
    if(colFitting)
        return;
    QHeaderView *h = twAct->horizontalHeader();
    const int delta = newSize - oldSize;
    const int partner = (col < COL_MSG) ? COL_MSG : col + 1;
    colFitting = true;
    if(partner < twAct->columnCount())
    {
        const int want = h->sectionSize(partner) - delta;
        const int got = qMax(h->minimumSectionSize(), want);
        h->resizeSection(partner, got);
        if(got != want)
            h->resizeSection(col, h->sectionSize(col) - (got - want));     // the partner at its least: no further
    }
    else
        h->resizeSection(col, oldSize);         // the last border is the edge of the window
    colFitting = false;
    fitColumns();
}

bool FtPanel::eventFilter(QObject *o, QEvent *e)
{
    if(o == twAct->viewport() && e->type() == QEvent::Resize)
        QTimer::singleShot(0, this, [this]() { fitColumns(); });
    return QWidget::eventFilter(o, e);
}
