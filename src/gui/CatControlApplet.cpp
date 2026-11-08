#include "CatControlApplet.h"

#include <QCoreApplication>
#include <QEvent>
#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/Theme.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

#ifdef HAVE_SERIALPORT
#include <QSerialPortInfo>
#ifdef Q_OS_WIN
#include "SerialPortCombo.h"
#endif
#endif

namespace AetherSDR {

namespace {

// Stylesheet templates — tokens are resolved at runtime by ThemeManager.
// Use ThemeManager::applyStyleSheet(widget, kXxx) so widgets re-style automatically
// on theme changes.
//
// The Enable CAT toggle was previously styled by an inline `kGreenToggle`
// constant; it now uses the canonical applyToggleButtonStyle helper with
// ToggleTribe::Success — same visual result, single source of truth.

const char* kHintBtn =
    "QPushButton { background: transparent; border: none; color: {{color.text.label}};"
    " font-size: 10px; font-style: italic; text-align: right; padding: 0; }"
    "QPushButton:hover { color: {{color.text.secondary}}; }";

const char* kHeaderStyle =
    "QLabel { color: {{color.text.label}}; font-size: 10px; font-weight: bold; }";

const char* kDimLabel =
    "QLabel { color: {{color.text.secondary}}; font-size: 11px; }";

const char* kMonoLabel =
    "QLabel { color: {{color.text.label}}; font-size: 10px; font-family: monospace; }";

const char* kPortEdit =
    "QLineEdit { font-size: 11px; background: {{color.background.0}}; border: 1px solid {{color.border.subtle}};"
    " border-radius: 3px; padding: 0px 3px; color: {{color.text.primary}}; }"
    "QLineEdit:read-only { color: {{color.text.label}}; background: {{color.background.0}}; border-color: {{color.border.subtle}}; }";

const char* kComboStyle =
    "QComboBox { font-size: 11px; background: {{color.background.1}}; border: 1px solid {{color.border.subtle}};"
    " border-radius: 3px; padding: 0px 3px; color: {{color.text.primary}}; }"
    "QComboBox:disabled { color: {{color.text.label}}; background: {{color.background.0}}; }"
    "QComboBox::drop-down { border: none; }"
    "QComboBox QAbstractItemView { background: {{color.background.1}}; color: {{color.text.primary}}; }"
    // Editable combos (VFO A/B) — transparent line edit so it inherits the combo look
    "QComboBox QLineEdit { background: transparent; border: none; color: {{color.text.primary}};"
    " font-size: 11px; padding: 0px; }"
    "QComboBox:disabled QLineEdit { color: {{color.text.label}}; }";

const char* kSepStyle =
    "QFrame { color: {{color.border.subtle}}; }";

// Per-port enable checkbox. The default indicator is nearly invisible against
// the dark applet background, so users miss that each CAT port has its own
// enable toggle (#cat-port-enable). Give it a high-contrast border and a filled
// accent when checked so the on/off state reads at a glance.
const char* kEnableCheck =
    "QCheckBox::indicator { width: 15px; height: 15px; border-radius: 3px;"
    " border: 1px solid {{color.text.secondary}}; background: {{color.background.0}}; }"
    "QCheckBox::indicator:hover { border-color: {{color.accent}}; }"
    "QCheckBox::indicator:checked { border: 1px solid {{color.accent}}; background: {{color.accent}}; }"
    "QCheckBox::indicator:disabled { border-color: {{color.border.subtle}}; background: {{color.background.1}}; }";

constexpr int kMinPort = 1024;
constexpr int kMaxPort = 65535;

// ANAN port (build 191): the CAT serial port column. Each OS lists its ports
// its own way (populateSerialCombo); the list is re-read every time the popup
// opens, so a USB adapter or a com0com pair added later shows up.
class RefreshOnPopupCombo : public QComboBox {
public:
    std::function<void()> beforePopup;
    void showPopup() override
    {
        if (beforePopup) beforePopup();
        QComboBox::showPopup();
    }
};

const int kSerialBauds[] = {4800, 9600, 19200, 38400, 57600, 115200};

#ifdef Q_OS_WIN
const char* kSerialTip =
    "CAT on a COM port (Windows).\n"
    "For a logger on this PC, install a virtual null-modem pair (com0com or\n"
    "VSPE): choose one end here (e.g. COM11) and the other end in the logger\n"
    "(COM12), radio type Kenwood TS-2000 (dialect TS-2000 or Flex).\n"
    "A real COM port works the same. A port not present yet is retried every 3 s.\n"
    "Off = TCP only.";
#else
const char* kSerialTip =
    "PTY only: the logger opens the PTY path shown at the right (a virtual\n"
    "serial port, Hamlib: Kenwood TS-2000 on that path).\n"
    "Choose a device to serve CAT on a real serial port too (/dev/ttyUSB0 ...);\n"
    "your user must be in the 'dialout' group. A device not present yet is\n"
    "retried every 3 s.";
#endif

// Fill the serial combo for this OS; keeps `saved` selected (or "Off").
void populateSerialCombo(QComboBox* combo, const QString& saved)
{
    const QSignalBlocker b(combo);
    combo->clear();
#ifdef HAVE_SERIALPORT
    const auto ports = QSerialPortInfo::availablePorts();
#ifdef Q_OS_WIN
    // Windows: the COM ports present ("COM3 - USB Serial Port"), then COM1..32
    // not present, in number order, as in the PTT/CW serial setup.
    SerialPortCombo::populate(combo, nullptr, saved, ports);
    combo->insertItem(0, QStringLiteral("Off"), QString());
#else
    // Linux: the PTY is always there; real devices by their /dev path.
    combo->addItem(QStringLiteral("PTY only"), QString());
    for (const auto& info : ports) {
        const QString path = info.systemLocation();
        const QString desc = info.description();
        combo->addItem(desc.isEmpty() ? path : QStringLiteral("%1 - %2").arg(path, desc), path);
    }
    if (!saved.isEmpty() && combo->findData(saved) < 0)
        combo->addItem(QStringLiteral("%1 (not present)").arg(saved), saved);
#endif
    const int idx = saved.isEmpty() ? 0 : combo->findData(saved);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
#else
    combo->addItem(QStringLiteral("—"), QString());
    combo->setEnabled(false);
    combo->setToolTip(QStringLiteral("Built without Qt SerialPort"));
    (void)saved;
#endif
}

QString serialStateHtml(bool running, bool open, const QString& error)
{
    if (!running) return QStringLiteral("—");
    if (open)     return QStringLiteral("<span style='color:#3cb043'>&#9679;</span>");
    if (!error.isEmpty()) return QStringLiteral("<span style='color:#e03c31'>&#9679;</span>");
    return QStringLiteral("—");
}

QString sliceLetter(int idx)
{
    if (idx < 0 || idx > 25) return "—";
    return QString(QChar('A' + idx));
}

// Persist a VFO selection without losing a saved slice the combo can't currently
// display. The VFO combos are bounded by the radio's slice-receiver count, so a
// reconnect to a smaller radio can leave a previously-chosen slice unrepresentable;
// the combo then falls back to its first entry (slice A / "—"). In that case keep
// the stored value rather than clobbering it — it reappears when the slices return.
// A genuine, representable selection (including a deliberate "—") is persisted.
void persistVfo(AppSettings& s, const QString& key, QComboBox* combo, int fallbackData)
{
    const int cur   = combo->currentData().toInt();
    const int saved = s.value(key, QString::number(fallbackData)).toInt();
    if (cur == fallbackData && saved != fallbackData && combo->findData(saved) < 0)
        return;   // combo fell back only because `saved` isn't available — preserve it
    s.setValue(key, QString::number(cur));
}

} // namespace

// ── Constructor ──────────────────────────────────────────────────────────────

CatControlApplet::CatControlApplet(QWidget* parent) : QWidget(parent)
{
    theme::setContainer(this, QStringLiteral("applet/cat"));
    setStyleSheet("QWidget { background: transparent; }");

    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    // Docked page: always present, narrow
    m_dockedPage = new QWidget(this);
    buildDockedView(m_dockedPage);
    m_rootLayout->addWidget(m_dockedPage);

    // Floating page is built lazily on first setFloating(true) — not added here,
    // so it cannot influence the docked sizeHint.

    hide();
}

// ── Docked page ──────────────────────────────────────────────────────────────

void CatControlApplet::buildDockedView(QWidget* page)
{
    page->setStyleSheet("QWidget { background: transparent; }");
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(6);

    auto* row = new QHBoxLayout;
    row->setSpacing(6);

    const bool catEnabled = AppSettings::instance().value("CatEnabled", "False").toString() == "True";
    m_enableBtn = new QPushButton(catEnabled ? "Enabled" : "Disabled");
    m_enableBtn->setCheckable(true);
    applyToggleButtonStyle(m_enableBtn, ToggleTribe::Success);
    m_enableBtn->setFixedSize(100, 22);
    {
        QSignalBlocker b(m_enableBtn);
        m_enableBtn->setChecked(catEnabled);
    }
    row->addWidget(m_enableBtn);
    row->addStretch();
    root->addLayout(row);

    root->addStretch();

    auto* hint = new QPushButton("Pop out for configuration.");
    ThemeManager::instance().applyStyleSheet(hint, kHintBtn);
    hint->setFlat(true);
    hint->setCursor(Qt::PointingHandCursor);
    // Clicking the hint pops out the applet — but the actual pop-out is driven
    // by the ContainerWidget's float button. The hint is informational only.
    root->addWidget(hint);

    connect(m_enableBtn, &QPushButton::toggled, this, [this](bool on) {
        m_enableBtn->setText(on ? "Enabled" : "Disabled");
        if (m_floatingEnableBtn) {
            m_floatingEnableBtn->setText(on ? "Enabled" : "Disabled");
        }
        auto& s = AppSettings::instance();
        s.setValue("CatEnabled", on ? "True" : "False");
        s.save();
        emit enableChanged(on);
    });
}

// ── Public API ───────────────────────────────────────────────────────────────

void CatControlApplet::setCatEnabled(bool on)
{
    if (m_enableBtn) {
        QSignalBlocker b(m_enableBtn);
        m_enableBtn->setChecked(on);
        m_enableBtn->setText(on ? "Enabled" : "Disabled");
    }
    if (m_floatingEnableBtn) {
        QSignalBlocker b(m_floatingEnableBtn);
        m_floatingEnableBtn->setChecked(on);
        m_floatingEnableBtn->setText(on ? "Enabled" : "Disabled");
    }
    for (int i = 0; i < m_rows.size(); ++i)
        updateRowLocked(i);
}

void CatControlApplet::setFloating(bool on)
{
    if (on) {
        // Build the floating page on first use
        if (!m_floatingPage) {
            m_floatingPage = new QWidget(this);
            m_floatingPage->setStyleSheet("QWidget { background: transparent; }");
            auto* floatingRoot = new QVBoxLayout(m_floatingPage);
            floatingRoot->setContentsMargins(4, 4, 4, 4);
            floatingRoot->setSpacing(4);

            // Enable button + note (must enable before ports can be configured)
            auto* enableRow = new QHBoxLayout;
            enableRow->setSpacing(8);
            const bool floatingCatEnabled =
                AppSettings::instance().value("CatEnabled", "False").toString() == "True";
            m_floatingEnableBtn = new QPushButton(floatingCatEnabled ? "Enabled" : "Disabled");
            m_floatingEnableBtn->setCheckable(true);
            applyToggleButtonStyle(m_floatingEnableBtn, ToggleTribe::Success);
            m_floatingEnableBtn->setFixedSize(100, 22);
            {
                QSignalBlocker b(m_floatingEnableBtn);
                m_floatingEnableBtn->setChecked(floatingCatEnabled);
            }
            auto* noteLabel = new QLabel("Enable before configuring ports.");
            ThemeManager::instance().applyStyleSheet(noteLabel,
                "QLabel { color: {{color.text.label}}; font-size: 10px; font-style: italic; }");
            enableRow->addWidget(m_floatingEnableBtn);
            enableRow->addWidget(noteLabel);
            enableRow->addStretch();
            floatingRoot->addLayout(enableRow);

            connect(m_floatingEnableBtn, &QPushButton::toggled, this, [this](bool on) {
                m_floatingEnableBtn->setText(on ? "Enabled" : "Disabled");
                auto& s = AppSettings::instance();
                s.setValue("CatEnabled", on ? "True" : "False");
                s.save();
                if (m_enableBtn) {
                    QSignalBlocker b(m_enableBtn);
                    m_enableBtn->setChecked(on);
                    m_enableBtn->setText(on ? "Enabled" : "Disabled");
                }
                emit enableChanged(on);
            });

            auto* sep = new QFrame;
            sep->setFrameShape(QFrame::HLine);
            ThemeManager::instance().applyStyleSheet(sep, kSepStyle);
            floatingRoot->addWidget(sep);

            auto* scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setStyleSheet("QScrollArea { background: transparent; }");

            auto* rowContainer = new QWidget;
            rowContainer->setStyleSheet("QWidget { background: transparent; }");
            m_grid = new QGridLayout(rowContainer);
            m_grid->setContentsMargins(0, 0, 0, 0);
            m_grid->setSpacing(3);
            scroll->setWidget(rowContainer);
            floatingRoot->addWidget(scroll);

            m_rootLayout->addWidget(m_floatingPage);
        }

        if (!m_rowsBuilt && m_portCount > 0)
            buildTableRows();

        m_dockedPage->hide();
        m_floatingPage->show();
    } else {
        if (m_floatingPage) m_floatingPage->hide();
        m_dockedPage->show();
    }
}

void CatControlApplet::setPorts(CatPort** ports, int count)
{
    m_portCount = qMin(count, kMaxPorts);
    for (int i = 0; i < m_portCount; ++i)
        m_ports[i] = ports[i];
    for (int i = m_portCount; i < kMaxPorts; ++i)
        m_ports[i] = nullptr;
    m_rowsBuilt = false;
    // If already floating, rebuild immediately
    if (m_floatingPage && m_floatingPage->isVisible())
        buildTableRows();
}

void CatControlApplet::setMaxSlices(int n)
{
    m_maxSlices = qMax(1, n);
    for (int row = 0; row < m_rows.size(); ++row) {
        PortRow& r = m_rows[row];
        // Remember the selected slice by DATA (not raw index): with the "—" entry
        // shifting indices, an index-based restore + qMin clamp could silently
        // repoint a VFO to a different slice when the count changes. Restore by
        // data; if the saved slice is no longer available, fall back to the first
        // entry (slice A / "—") for DISPLAY only — the persisted value is kept
        // (persistVfo won't overwrite an unrepresentable saved slice).
        const int savedA = r.vfoACombo->currentData().toInt();
        const int savedB = r.vfoBCombo->currentData().toInt();
        r.vfoACombo->blockSignals(true);
        r.vfoBCombo->blockSignals(true);
        r.vfoACombo->clear();
        r.vfoBCombo->clear();
        populateVfoCombo(r.vfoACombo, false);
        populateVfoCombo(r.vfoBCombo, true);
        const int idxA = r.vfoACombo->findData(savedA);
        const int idxB = r.vfoBCombo->findData(savedB);
        r.vfoACombo->setCurrentIndex(idxA >= 0 ? idxA : 0);
        r.vfoBCombo->setCurrentIndex(idxB >= 0 ? idxB : 0);
        r.vfoACombo->blockSignals(false);
        r.vfoBCombo->blockSignals(false);
    }
}

// ── Build floating table rows ────────────────────────────────────────────────

void CatControlApplet::populateVfoCombo(QComboBox* combo, bool includeNone)
{
    if (includeNone) combo->addItem("—", CatPort::kVfoNone);
    for (int i = 0; i < m_maxSlices; ++i)
        combo->addItem(sliceLetter(i), i);
}

void CatControlApplet::buildTableRows()
{
    // Clear any existing rows
    while (QLayoutItem* item = m_grid->takeAt(0)) {
        if (item->widget()) item->widget()->setParent(nullptr);
        delete item;
    }
    m_rows.clear();

    // Header labels as grid row 0 — same layout as data rows, so columns always align.
    // w=0 means no fixed width (used for the stretching PTY column).
    auto addHdr = [&](const QString& text, int w, int col,
                      Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
        auto* lbl = new QLabel(text);
        ThemeManager::instance().applyStyleSheet(lbl, kHeaderStyle);
        if (w > 0) lbl->setFixedWidth(w);
        lbl->setAlignment(align);
        m_grid->addWidget(lbl, 0, col);
    };
    int hcol = 0;
    addHdr("Enabled",  54, hcol++, Qt::AlignHCenter | Qt::AlignVCenter);
    addHdr("Port",     50, hcol++, Qt::AlignHCenter | Qt::AlignVCenter);
    addHdr("Dialect",  84, hcol++);
    addHdr("VFO A",    42, hcol++, Qt::AlignHCenter | Qt::AlignVCenter);
    addHdr("VFO B",    42, hcol++, Qt::AlignHCenter | Qt::AlignVCenter);
    addHdr("Serial",  130, hcol++);
    addHdr("Baud",     62, hcol++);
    addHdr("",         14, hcol++);  // serial port state dot
#ifndef Q_OS_WIN
    const int ptyCol = hcol;
    addHdr("PTY",       0, hcol++);  // no fixed width — column stretches
#endif
    addHdr("Clients",  48, hcol++);

    auto& settings = AppSettings::instance();

    for (int i = 0; i < m_portCount; ++i) {
        PortRow row;
        const QString prefix = QString("CatPort_%1_").arg(i);

        // Enable checkbox
        row.enableCheck = new QCheckBox;
        ThemeManager::instance().applyStyleSheet(row.enableCheck, kEnableCheck);
        row.enableCheck->setToolTip("Enable this CAT port");
        {
            bool en = settings.value(prefix + "Enabled", "False").toString() == "True";
            QSignalBlocker b(row.enableCheck);
            row.enableCheck->setChecked(en);
        }

        // Port edit
        row.portEdit = new QLineEdit;
        ThemeManager::instance().applyStyleSheet(row.portEdit, kPortEdit);
        row.portEdit->setFixedWidth(50);
        row.portEdit->setAlignment(Qt::AlignCenter);
        row.portEdit->setValidator(new QIntValidator(kMinPort, kMaxPort, row.portEdit));
        row.portEdit->setPlaceholderText("port");
        {
            QString portStr = settings.value(prefix + "Port", "").toString();
            QSignalBlocker b(row.portEdit);
            row.portEdit->setText(portStr);
        }

        // Dialect combo
        row.dialectCombo = new QComboBox;
        ThemeManager::instance().applyStyleSheet(row.dialectCombo, kComboStyle);
        row.dialectCombo->setFixedWidth(84);
        row.dialectCombo->addItem("Rigctld",  static_cast<int>(CatDialect::Rigctld));
        row.dialectCombo->addItem("TS-2000",  static_cast<int>(CatDialect::TS2000));
        row.dialectCombo->addItem("Flex",     static_cast<int>(CatDialect::FlexCAT));
        row.dialectCombo->addItem("Thetis",   static_cast<int>(CatDialect::Thetis));
        row.dialectCombo->setToolTip(
            "Rigctld: Hamlib NET rigctl (TCP / PTY).\n"
            "TS-2000: Kenwood TS-2000 (ID019) - the safe choice for any logger.\n"
            "Flex: TS-2000 + FlexRadio SmartSDR ZZ commands.\n"
            "Thetis: TS-2000 + Thetis / PowerSDR ZZ commands (ID019),\n"
            "for a logger set up for Thetis or PowerSDR.");
        {
            const CatDialect d = catDialectFromKey(settings.value(prefix + "Dialect", "Rigctld").toString());
            const int found = row.dialectCombo->findData(static_cast<int>(d));
            const int idx = found >= 0 ? found : 0;
            QSignalBlocker b(row.dialectCombo);
            row.dialectCombo->setCurrentIndex(idx);
        }

        // VFO A combo — editable+read-only so we can center the selected-item text
        row.vfoACombo = new QComboBox;
        ThemeManager::instance().applyStyleSheet(row.vfoACombo, kComboStyle);
        row.vfoACombo->setFixedWidth(42);
        populateVfoCombo(row.vfoACombo, false);
        {
            int vfoA = settings.value(prefix + "VfoA", "0").toInt();
            int idx = row.vfoACombo->findData(vfoA);
            QSignalBlocker b(row.vfoACombo);
            row.vfoACombo->setCurrentIndex(qMax(0, idx));
        }
        row.vfoACombo->setEditable(true);
        row.vfoACombo->lineEdit()->setAlignment(Qt::AlignCenter);
        row.vfoACombo->lineEdit()->setReadOnly(true);
        row.vfoACombo->lineEdit()->setFocusPolicy(Qt::NoFocus);

        // VFO B combo
        row.vfoBCombo = new QComboBox;
        ThemeManager::instance().applyStyleSheet(row.vfoBCombo, kComboStyle);
        row.vfoBCombo->setFixedWidth(42);
        populateVfoCombo(row.vfoBCombo, true);
        {
            int vfoB = settings.value(prefix + "VfoB",
                                      QString::number(CatPort::kVfoNone)).toInt();
            int idx = row.vfoBCombo->findData(vfoB);
            QSignalBlocker b(row.vfoBCombo);
            row.vfoBCombo->setCurrentIndex(qMax(0, idx));
        }
        row.vfoBCombo->setEditable(true);
        row.vfoBCombo->lineEdit()->setAlignment(Qt::AlignCenter);
        row.vfoBCombo->lineEdit()->setReadOnly(true);
        row.vfoBCombo->lineEdit()->setFocusPolicy(Qt::NoFocus);

        // Serial port (build 191) — per-OS list, refreshed when it opens
        {
            auto* sc = new RefreshOnPopupCombo;
            row.serialCombo = sc;
            ThemeManager::instance().applyStyleSheet(sc, kComboStyle);
            sc->setFixedWidth(130);
            sc->view()->setMinimumWidth(260);
            sc->setToolTip(QString::fromUtf8(kSerialTip));
            populateSerialCombo(sc, settings.value(prefix + "Serial", "").toString().trimmed());
            sc->beforePopup = [sc]() {
                populateSerialCombo(sc, sc->currentData().toString());
            };
        }
        row.baudCombo = new QComboBox;
        ThemeManager::instance().applyStyleSheet(row.baudCombo, kComboStyle);
        row.baudCombo->setFixedWidth(62);
        row.baudCombo->setToolTip("Serial speed, 8N1. Set the same in the logger;\n"
                                  "a virtual pair (com0com, PTY) ignores it.");
        for (int baud : kSerialBauds)
            row.baudCombo->addItem(QString::number(baud), baud);
        {
            const int baud = settings.value(prefix + "SerialBaud", "9600").toInt();
            const int idx = row.baudCombo->findData(baud);
            QSignalBlocker b(row.baudCombo);
            row.baudCombo->setCurrentIndex(idx >= 0 ? idx : 1);
        }
        row.serialState = new QLabel("—");
        ThemeManager::instance().applyStyleSheet(row.serialState, kDimLabel);
        row.serialState->setFixedWidth(14);
        row.serialState->setAlignment(Qt::AlignCenter);
        row.serialState->setTextFormat(Qt::RichText);
        if (m_ports[i]) {
            auto updateSerial = [this, i]() {
                if (i >= m_rows.size()) return;
                CatPort* p = m_ports[i];
                const bool active = p->isRunning() && !p->serialPortName().isEmpty();
                m_rows[i].serialState->setText(
                    serialStateHtml(active, p->serialOpen(), p->serialError()));
                m_rows[i].serialState->setToolTip(
                    !active ? QString()
                    : p->serialOpen() ? QStringLiteral("%1 open").arg(p->serialPortName())
                    : QStringLiteral("%1: %2 (retrying)").arg(p->serialPortName(),
                                                              p->serialError()));
            };
            {   // initial state: the port may already be running
                CatPort* p = m_ports[i];
                const bool active = p->isRunning() && !p->serialPortName().isEmpty();
                row.serialState->setText(serialStateHtml(active, p->serialOpen(), p->serialError()));
            }
            connect(m_ports[i], &CatPort::serialStateChanged, this,
                    [updateSerial](bool, const QString&) { updateSerial(); });
            connect(m_ports[i], &CatPort::clientCountChanged, this,
                    [updateSerial](int) { updateSerial(); });
        }

        // PTY label — sizes to the actual path so the column is always wide enough
        row.ptyLabel = new QLabel("—");
#ifndef Q_OS_WIN
        ThemeManager::instance().applyStyleSheet(row.ptyLabel, kMonoLabel);
        // No fixed or minimum width — sizeHint is driven by the path text,
        // so the grid column sizes itself dynamically to the actual path length.
        if (m_ports[i]) {
            QString path = m_ports[i]->ptyPath();
            row.ptyLabel->setText(path.isEmpty() ? "—" : path);
            connect(m_ports[i], &CatPort::ptyPathChanged, this,
                    [this, i](const QString& path) {
                        if (i < m_rows.size())
                            m_rows[i].ptyLabel->setText(path.isEmpty() ? "—" : path);
                    });
        }
        row.ptyLabel->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(row.ptyLabel, &QLabel::customContextMenuRequested, this,
                [this, i](const QPoint& pos) {
                    if (i >= m_rows.size()) return;
                    const QString path = m_rows[i].ptyLabel->text();
                    if (path.isEmpty() || path == "—") return;
                    QMenu menu;
                    QAction* act = menu.addAction("Copy PTY Name");
                    if (menu.exec(m_rows[i].ptyLabel->mapToGlobal(pos)) == act)
                        QGuiApplication::clipboard()->setText(path);
                });
#else
        row.ptyLabel->hide();
#endif

        // Clients label
        row.clientLabel = new QLabel("—");
        ThemeManager::instance().applyStyleSheet(row.clientLabel, kDimLabel);
        row.clientLabel->setFixedWidth(48);
        row.clientLabel->setAlignment(Qt::AlignCenter);
        if (m_ports[i]) {
            auto updateClients = [this, i]() {
                if (i >= m_rows.size()) return;
                int n = m_ports[i]->clientCount();
                m_rows[i].clientLabel->setText(
                    m_ports[i]->isRunning() ? QString::number(n) : "—");
            };
            updateClients();  // initialize — signal alone won't fire until first connect/disconnect
            connect(m_ports[i], &CatPort::clientCountChanged, this, updateClients);
        }

        // Grid layout — data rows start at 1 (row 0 is the header)
        int col = 0;
        m_grid->addWidget(row.enableCheck,  i+1, col++, Qt::AlignHCenter);
        m_grid->addWidget(row.portEdit,     i+1, col++);
        m_grid->addWidget(row.dialectCombo, i+1, col++);
        m_grid->addWidget(row.vfoACombo,    i+1, col++);
        m_grid->addWidget(row.vfoBCombo,    i+1, col++);
        m_grid->addWidget(row.serialCombo,  i+1, col++);
        m_grid->addWidget(row.baudCombo,    i+1, col++);
        m_grid->addWidget(row.serialState,  i+1, col++, Qt::AlignHCenter);
#ifndef Q_OS_WIN
        m_grid->addWidget(row.ptyLabel,     i+1, col++);
#endif
        m_grid->addWidget(row.clientLabel,  i+1, col++);

        // Connections
        const int capturedI = i;

        connect(row.enableCheck, &QCheckBox::toggled, this,
                [this, capturedI](bool) {
                    applyRowToSettings(capturedI);
                    updateRowLocked(capturedI);
                    emit configChanged();
                });

        connect(row.portEdit, &QLineEdit::editingFinished, this,
                [this, capturedI]() {
                    applyRowToSettings(capturedI);
                    updateRowLocked(capturedI);
                    emit configChanged();
                });

        connect(row.dialectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, capturedI](int) {
                    restoreVfoBForDialect(capturedI);  // switch to dual-VFO → restore saved VFO B
                    updateRowLocked(capturedI);        // single-VFO → force "—" + disable selectors
                    applyRowToSettings(capturedI);
                    emit configChanged();
                });

        connect(row.vfoACombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, capturedI](int) {
                    applyRowToSettings(capturedI);
                    emit configChanged();
                });

        connect(row.vfoBCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, capturedI](int) {
                    applyRowToSettings(capturedI);
                    emit configChanged();
                });

        connect(row.serialCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, capturedI](int) {
                    applyRowToSettings(capturedI);
                    updateRowLocked(capturedI);
                    emit configChanged();
                });

        connect(row.baudCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, capturedI](int) {
                    applyRowToSettings(capturedI);
                    emit configChanged();
                });

        m_rows.append(row);
        updateRowLocked(i);
    }

#ifndef Q_OS_WIN
    m_grid->setColumnStretch(ptyCol, 1);  // PTY column expands to fill available width
#endif
    m_grid->setRowStretch(m_portCount + 1, 1);
    m_rowsBuilt = true;
    // Build 216: the table sits in a scroll area, which does not pass a size
    // change up; ask the floating window to fit its height to the rows.
    if (QWidget* w = window(); w && w != this)
        QCoreApplication::postEvent(w, new QEvent(QEvent::LayoutRequest));
}

// ── Lock controls while port is running ──────────────────────────────────────

void CatControlApplet::updateRowLocked(int row)
{
    if (row >= m_rows.size()) return;
    PortRow& r = m_rows[row];

    // A serial port alone is enough to run the port (build 191).
    bool hasPort  = !r.portEdit->text().isEmpty()
                 || !r.serialCombo->currentData().toString().isEmpty();
    bool masterOn = AppSettings::instance().value("CatEnabled", "False").toString() == "True";
    // Lock on UI state only — isRunning() lags behind the checkbox by one applyCatPortCount cycle.
    bool locked   = masterOn && r.enableCheck->isChecked() && hasPort;

    // A single-VFO dialect (rigctld) has no VFO B — force the selector to "—" and
    // disable it. DISPLAY override only: the saved VfoB is preserved (applyRowToSettings
    // skips persisting it for single-VFO dialects) and restored by restoreVfoBForDialect()
    // when switching back to a dual-VFO dialect. This runs on every lock refresh, so it
    // is kept idempotent (display state only) — the restore lives in the dialect handler.
    const auto dialect = static_cast<CatDialect>(r.dialectCombo->currentData().toInt());
    const bool supportsVfoB = dialectSupportsVfoB(dialect);
    if (!supportsVfoB) {
        const int noneIdx = r.vfoBCombo->findData(CatPort::kVfoNone);
        if (noneIdx >= 0 && r.vfoBCombo->currentIndex() != noneIdx) {
            QSignalBlocker b(r.vfoBCombo);
            r.vfoBCombo->setCurrentIndex(noneIdx);
        }
    }

    r.enableCheck->setEnabled(hasPort || !r.enableCheck->isChecked());
    r.portEdit->setReadOnly(locked);
    r.dialectCombo->setEnabled(!locked);
    r.vfoACombo->setEnabled(!locked);
    r.vfoBCombo->setEnabled(!locked && supportsVfoB);
    r.serialCombo->setEnabled(!locked && CatPort::serialSupported());
    r.baudCombo->setEnabled(!locked && CatPort::serialSupported());
}

// Restore the row's VFO B selector to the saved value when the (new) dialect
// supports VFO B. Called from the dialect-change handler: switching back from a
// single-VFO dialect brings the operator's preserved slice-B choice back into
// view. Display-only (signals blocked); a no-op for single-VFO dialects, which
// updateRowLocked() forces to "—".
void CatControlApplet::restoreVfoBForDialect(int row)
{
    if (row >= m_rows.size()) return;
    PortRow& r = m_rows[row];
    const auto dialect = static_cast<CatDialect>(r.dialectCombo->currentData().toInt());
    if (!dialectSupportsVfoB(dialect)) return;
    const QString prefix = QString("CatPort_%1_").arg(row);
    const int savedB = AppSettings::instance()
                           .value(prefix + "VfoB", QString::number(CatPort::kVfoNone)).toInt();
    const int idx = r.vfoBCombo->findData(savedB);
    if (idx >= 0 && r.vfoBCombo->currentIndex() != idx) {
        QSignalBlocker b(r.vfoBCombo);
        r.vfoBCombo->setCurrentIndex(idx);
    }
}

// ── Persist row settings ─────────────────────────────────────────────────────

void CatControlApplet::applyRowToSettings(int row)
{
    if (row >= m_rows.size()) return;
    const PortRow& r = m_rows[row];
    auto& s = AppSettings::instance();
    const QString prefix = QString("CatPort_%1_").arg(row);

    s.setValue(prefix + "Enabled", r.enableCheck->isChecked() ? "True" : "False");
    s.setValue(prefix + "Port",    r.portEdit->text());
    s.setValue(prefix + "Serial",  r.serialCombo->currentData().toString());
    s.setValue(prefix + "SerialBaud", QString::number(r.baudCombo->currentData().toInt()));

    const auto dialect = static_cast<CatDialect>(r.dialectCombo->currentData().toInt());
    const QString dialectKey = catDialectKey(dialect);
    s.setValue(prefix + "Dialect", dialectKey);
    // VFO A always applies; persistVfo preserves a saved slice the combo can't
    // currently display (smaller radio) instead of clobbering it.
    persistVfo(s, prefix + "VfoA", r.vfoACombo, 0);
    // VFO B only for dual-VFO dialects. For a single-VFO dialect (rigctld) the
    // selector is forced to "—"; persisting that would clobber the operator's
    // saved VFO B. Skipping it leaves the stored value intact for a lossless
    // dialect round-trip (restoreVfoBForDialect restores the selector on the way
    // back).
    if (dialectSupportsVfoB(dialect))
        persistVfo(s, prefix + "VfoB", r.vfoBCombo, CatPort::kVfoNone);
    s.save();
}

} // namespace AetherSDR
