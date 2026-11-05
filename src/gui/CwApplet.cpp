#include "CwApplet.h"

#include "ComboStyle.h"
#include "GuardedSlider.h"
#include "Theme.h"
#include "core/AppSettings.h"
#include "core/ThemeManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace AetherSDR {

namespace {

// AppSettings keys (Hl2Backend::ananPushCwKeyer reads the same ones).
constexpr const char* kMode      = "AnanKeyerMode";        // 0..2, default 2 (iambic B)
constexpr const char* kSpeed     = "AnanKeyerSpeed";       // 10..63 WPM, 20
constexpr const char* kWeight    = "AnanKeyerWeight";      // 33..66, 50
constexpr const char* kSwap      = "AnanPaddleSwap";       // True/False
constexpr const char* kAutoSpace = "AnanKeyerAutoSpace";   // True/False
constexpr const char* kStLevel   = "AnanSideToneLevelPct"; // 0..99 %, 50
constexpr const char* kStFreq    = "AnanSideToneFreq";     // 400..1000 Hz, 700
constexpr const char* kHang      = "AnanHangTime";         // 10..1000 ms, 250
constexpr const char* kDemo      = "AnanCwDemo";           // True/False

constexpr int kLabelW = 64;
constexpr int kValueW = 46;

QString tf(bool b) { return b ? QStringLiteral("True") : QStringLiteral("False"); }

bool flag(const char* key)
{
    return AppSettings::instance().value(QLatin1String(key), QStringLiteral("False")).toString()
           == QLatin1String("True");
}

int intSetting(const char* key, int def, int lo, int hi)
{
    return std::clamp(AppSettings::instance().value(QLatin1String(key), def).toInt(), lo, hi);
}

void store(const char* key, const QVariant& v)
{
    auto& s = AppSettings::instance();
    s.setValue(QLatin1String(key), v);
    s.save();
}

QLabel* rowLabel(const QString& text)
{
    auto* l = new QLabel(text);
    ThemeManager::instance().applyStyleSheet(
        l, "QLabel { color: {{color.text.secondary}}; font-size: 11px; }");
    l->setFixedWidth(kLabelW);
    return l;
}

QLabel* valueLabel()
{
    auto* l = new QLabel;
    ThemeManager::instance().applyStyleSheet(
        l, "QLabel { color: {{color.text.primary}}; font-size: 11px; }");
    l->setFixedWidth(kValueW);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return l;
}

QLabel* sectionLabel(const QString& text)
{
    auto* l = new QLabel(text);
    ThemeManager::instance().applyStyleSheet(
        l, "QLabel { color: {{color.text.primary}}; font-size: 11px; font-weight: bold; }");
    return l;
}

QCheckBox* checkBox(const QString& text)
{
    auto* c = new QCheckBox(text);
    ThemeManager::instance().applyStyleSheet(
        c, "QCheckBox { color: {{color.text.secondary}}; font-size: 11px; }");
    return c;
}

QComboBox* combo(const QStringList& items)
{
    auto* c = new QComboBox;
    c->addItems(items);
    applyComboStyle(c);
    return c;
}

// label + slider + value, one row.
QSlider* sliderRow(QVBoxLayout* vbox, const QString& name, int lo, int hi, int step,
                   QLabel** value)
{
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    row->addWidget(rowLabel(name));
    auto* s = new GuardedSlider(Qt::Horizontal);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setPageStep(step);
    applyPrimarySliderStyle(s);
    row->addWidget(s, 1);
    *value = valueLabel();
    row->addWidget(*value);
    vbox->addLayout(row);
    return s;
}

QComboBox* comboRow(QVBoxLayout* vbox, const QString& name, const QStringList& items)
{
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    row->addWidget(rowLabel(name));
    auto* c = combo(items);
    row->addWidget(c, 1);
    vbox->addLayout(row);
    return c;
}

} // namespace

CwApplet::CwApplet(QWidget* parent)
    : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 2, 4, 4);
    vbox->setSpacing(3);

    // ── Keyer ───────────────────────────────────────────────────────────
    vbox->addWidget(sectionLabel(tr("Keyer")));
    m_mode = comboRow(vbox, tr("Mode:"),
                      {tr("Straight key"), tr("Iambic mode A"), tr("Iambic mode B")});
    m_mode->setToolTip(tr("Keyer mode"));
    m_speed = sliderRow(vbox, tr("Speed:"), 10, 63, 1, &m_speedValue);
    m_speed->setToolTip(tr("Keyer speed, 10..63 WPM"));
    m_weight = sliderRow(vbox, tr("Weight:"), 33, 66, 1, &m_weightValue);
    m_weight->setToolTip(tr("Keyer weight, 33..66 (50 = normal)"));
    m_swap = checkBox(tr("Paddle swap"));
    vbox->addWidget(m_swap);
    m_autoSpace = checkBox(tr("Keyer auto letter spacing"));
    m_autoSpace->setToolTip(
        tr("A pause longer than one dot is made a letter space"));
    vbox->addWidget(m_autoSpace);

    // ── Transmit ────────────────────────────────────────────────────────
    vbox->addSpacing(2);
    vbox->addWidget(sectionLabel(tr("Transmit")));
    m_hang = sliderRow(vbox, tr("Hang time:"), 10, 1000, 10, &m_hangValue);
    m_hang->setToolTip(tr("The relay stays on after the last CW element, 10..1000 ms"));
    m_demo = checkBox(tr("CW demo (CW without RF)"));
    m_demo->setToolTip(tr("CW without RF: the relay and the PA stay off"));
    vbox->addWidget(m_demo);

    // ── Side tone ───────────────────────────────────────────────────────
    vbox->addSpacing(2);
    vbox->addWidget(sectionLabel(tr("Side tone")));
    m_stLevel = sliderRow(vbox, tr("Level:"), 0, 99, 1, &m_stLevelValue);
    m_stLevel->setToolTip(tr("Side tone level, 0..99 % (0 = off)"));
    m_stFreq = sliderRow(vbox, tr("Frequency:"), 400, 1000, 10, &m_stFreqValue);
    m_stFreq->setToolTip(tr("Side tone frequency, 400..1000 Hz"));

    // ── Load ────────────────────────────────────────────────────────────
    m_loading = true;
    m_mode->setCurrentIndex(intSetting(kMode, 2, 0, 2));
    m_speed->setValue(intSetting(kSpeed, 20, 10, 63));
    m_weight->setValue(intSetting(kWeight, 50, 33, 66));
    m_swap->setChecked(flag(kSwap));
    m_autoSpace->setChecked(flag(kAutoSpace));
    m_hang->setValue(intSetting(kHang, 250, 10, 1000) / 10 * 10);
    m_demo->setChecked(flag(kDemo));
    m_stLevel->setValue(intSetting(kStLevel, 50, 0, 99));
    m_stFreq->setValue(intSetting(kStFreq, 700, 400, 1000) / 10 * 10);
    m_loading = false;

    auto showValues = [this] {
        m_speedValue->setText(tr("%1 WPM").arg(m_speed->value()));
        m_weightValue->setText(QString::number(m_weight->value()));
        m_hangValue->setText(tr("%1 ms").arg(m_hang->value()));
        m_stLevelValue->setText(QStringLiteral("%1%").arg(m_stLevel->value()));
        m_stFreqValue->setText(tr("%1 Hz").arg(m_stFreq->value()));
    };
    showValues();

    // ── Save on change ──────────────────────────────────────────────────
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) { if (!m_loading) store(kMode, i); });
    connect(m_speed, &QSlider::valueChanged, this, [this, showValues](int v) {
        showValues();
        if (m_loading)
            return;
        store(kSpeed, v);
        emit keyerSpeedChanged(v);
    });
    connect(m_weight, &QSlider::valueChanged, this, [this, showValues](int v) {
        showValues();
        if (!m_loading) store(kWeight, v);
    });
    connect(m_swap, &QCheckBox::toggled, this,
            [this](bool on) { if (!m_loading) store(kSwap, tf(on)); });
    connect(m_autoSpace, &QCheckBox::toggled, this,
            [this](bool on) { if (!m_loading) store(kAutoSpace, tf(on)); });
    connect(m_hang, &QSlider::valueChanged, this, [this, showValues](int v) {
        // 10 ms steps, as MicronSDR (the wheel and drag land between them).
        const int snapped = (v + 5) / 10 * 10;
        if (snapped != v) {
            m_hang->setValue(snapped);
            return;
        }
        showValues();
        if (!m_loading) store(kHang, v);
    });
    connect(m_demo, &QCheckBox::toggled, this,
            [this](bool on) { if (!m_loading) store(kDemo, tf(on)); });
    connect(m_stLevel, &QSlider::valueChanged, this, [this, showValues](int v) {
        showValues();
        if (!m_loading) store(kStLevel, v);
    });
    connect(m_stFreq, &QSlider::valueChanged, this, [this, showValues](int v) {
        const int snapped = (v + 5) / 10 * 10;
        if (snapped != v) {
            m_stFreq->setValue(snapped);
            return;
        }
        showValues();
        if (!m_loading) store(kStFreq, v);
    });
}

void CwApplet::setKeyerSpeed(int wpm)
{
    const QSignalBlocker block(m_speed);
    m_speed->setValue(std::clamp(wpm, 10, 63));
    m_speedValue->setText(tr("%1 WPM").arg(m_speed->value()));
    store(kSpeed, m_speed->value());
}

int CwApplet::keyerSpeed() const
{
    return m_speed->value();
}

} // namespace AetherSDR
