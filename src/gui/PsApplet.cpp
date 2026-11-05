#include "PsApplet.h"

#include "ComboStyle.h"
#include "GuardedSlider.h"
#include "PsAmpViewDialog.h"
#include "Theme.h"
#include "core/ThemeManager.h"
#include "core/backends/anan/AnanPureSignal.h"
#include "core/backends/hl2/Hl2Backend.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr int kLabelW = 64;

QLabel* rowLabel(const QString& text)
{
    auto* l = new QLabel(text);
    ThemeManager::instance().applyStyleSheet(
        l, "QLabel { color: {{color.text.secondary}}; font-size: 11px; }");
    l->setFixedWidth(kLabelW);
    return l;
}

QLabel* sectionLabel(const QString& text)
{
    auto* l = new QLabel(text);
    ThemeManager::instance().applyStyleSheet(
        l, "QLabel { color: {{color.text.primary}}; font-size: 11px; font-weight: bold; }");
    return l;
}

const QString kBtnStyle = QStringLiteral(
    "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px;"
    " color: #c8d8e8; font-size: 11px; font-weight: bold; padding: 1px 4px; }"
    "QPushButton:hover { border: 1px solid #0090e0; }"
    "QPushButton:pressed { background: #0a5a90; border: 1px solid #40b0ff; color: #ffffff; }"
    "QPushButton:checked { background: #0070c0; border: 1px solid #0090e0; color: #ffffff; }"
    "QPushButton:disabled { color: #607080; }");
const QString kPsOnStyle = QStringLiteral(
    "QPushButton:checked { background: #1d8a3a; border: 1px solid #30c060; color: #ffffff; }");
const QString kTwoToneOnStyle = QStringLiteral(
    "QPushButton:checked { background: #c03030; border: 1px solid #ff5050; color: #ffffff; }");

QPushButton* button(const QString& text, bool checkable, const QString& extra = QString())
{
    auto* b = new QPushButton(text);
    b->setCheckable(checkable);
    b->setFixedHeight(22);
    b->setStyleSheet(kBtnStyle + extra);
    return b;
}

QLabel* lamp(const QString& text)
{
    auto* l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    l->setFixedHeight(18);
    l->setMinimumWidth(44);
    return l;
}

void setLamp(QLabel* l, const QString& bg, const QString& fg = QStringLiteral("#0b0e12"))
{
    l->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border: 1px solid #304050;"
                                    " border-radius: 2px; font-size: 10px; font-weight: bold; }")
                         .arg(bg, fg));
}

const char* kStateNames[] = {"RESET", "WAIT", "MOXDELAY", "SETUP", "COLLECT",
                             "MOXCHECK", "CALC", "DELAY", "STAYON", "TURNON"};

} // namespace

PsApplet::PsApplet(QWidget* parent)
    : QWidget(parent)
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 2, 4, 4);
    vbox->setSpacing(3);

    // ── Control ─────────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        m_psBtn = button(tr("PS"), true, kPsOnStyle);
        m_psBtn->setToolTip(tr("PureSignal on / off"));
        m_psBtn->setMinimumWidth(40);
        row->addWidget(m_psBtn);
        m_autoBtn = button(tr("Auto"), true);
        m_autoBtn->setToolTip(tr("Calibrate continuously while transmitting"));
        m_singleBtn = button(tr("Single"), true);
        m_singleBtn->setToolTip(tr("Keep the correction of one calibration (Calibrate)"));
        row->addWidget(m_autoBtn, 1);
        row->addWidget(m_singleBtn, 1);
        m_calBtn = button(tr("Calibrate"), true);
        m_calBtn->setToolTip(tr("One calibration now (Single); lit until it is applied"));
        row->addWidget(m_calBtn);
        m_resetBtn = button(tr("Reset"), false);
        m_resetBtn->setToolTip(tr("Drop the correction and start again"));
        row->addWidget(m_resetBtn);
        vbox->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        m_twoToneBtn = button(tr("2 Tone"), true, kTwoToneOnStyle);
        m_twoToneBtn->setToolTip(tr("Transmit a two-tone test signal (SSB only).\n"
                                    "PureSignal calibrates on it; click again to stop."));
        row->addWidget(m_twoToneBtn, 1);
        m_ampViewBtn = button(tr("Amp View..."), false);
        m_ampViewBtn->setToolTip(tr("Curves of the amplifier and of the correction, tests"));
        row->addWidget(m_ampViewBtn, 1);
        vbox->addLayout(row);
    }

    // ── Indicators ──────────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        m_feedLamp = lamp(tr("FEED"));
        m_feedLamp->setToolTip(tr("Feedback samples reaching the calibration"));
        m_fbLamp = lamp(tr("FB"));
        m_fbLamp->setToolTip(tr("Feedback level (Thetis): green 129..181 is right,\n"
                                "yellow / red too low, blue too high"));
        m_corrLamp = lamp(tr("CORR"));
        m_corrLamp->setToolTip(tr("Correction applied to the transmit signal"));
        row->addWidget(m_feedLamp);
        row->addWidget(m_fbLamp);
        row->addWidget(m_corrLamp);
        vbox->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        m_stateLabel = new QLabel;
        m_calLabel = new QLabel;
        for (QLabel* l : {m_stateLabel, m_calLabel})
            ThemeManager::instance().applyStyleSheet(
                l, "QLabel { color: {{color.text.secondary}}; font-size: 11px; }");
        row->addWidget(m_stateLabel, 1);
        row->addWidget(m_calLabel);
        vbox->addLayout(row);
    }

    // ── Feedback ────────────────────────────────────────────────────────
    vbox->addSpacing(2);
    vbox->addWidget(sectionLabel(tr("Feedback")));
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        row->addWidget(rowLabel(tr("Input:")));
        m_source = new QComboBox;
        m_source->addItems({tr("Internal coupler"), tr("EXT1"), tr("RX Bypass")});
        applyComboStyle(m_source);
        m_source->setToolTip(tr("Where the feedback reaches the receiver while transmitting"));
        row->addWidget(m_source, 1);
        vbox->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        row->addWidget(rowLabel(tr("ADC:")));
        m_fbAdc = new QComboBox;
        m_fbAdc->addItems({tr("ADC1"), tr("ADC2 (RX2 in)")});
        applyComboStyle(m_fbAdc);
        m_fbAdc->setToolTip(tr("The ADC that takes the PureSignal feedback while transmitting;\n"
                               "the attenuator below acts on it"));
        row->addWidget(m_fbAdc, 1);
        vbox->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        row->addWidget(rowLabel(tr("Attenuator:")));
        m_att = new GuardedSlider(Qt::Horizontal);
        m_att->setRange(0, 31);
        applyPrimarySliderStyle(m_att);
        m_att->setToolTip(tr("Feedback ADC step attenuator while transmitting, 0..31 dB"));
        row->addWidget(m_att, 1);
        m_attValue = new QLabel;
        m_attValue->setFixedWidth(38);
        m_attValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        ThemeManager::instance().applyStyleSheet(
            m_attValue, "QLabel { color: {{color.text.primary}}; font-size: 11px; }");
        row->addWidget(m_attValue);
        vbox->addLayout(row);
    }
    m_autoAtt = new QCheckBox(tr("Auto attenuate"));
    ThemeManager::instance().applyStyleSheet(
        m_autoAtt, "QCheckBox { color: {{color.text.secondary}}; font-size: 11px; }");
    m_autoAtt->setToolTip(tr("Keep the feedback level in range with the attenuator"));
    vbox->addWidget(m_autoAtt);

    // ── Wiring ──────────────────────────────────────────────────────────
    connect(m_psBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        if (auto* ps = pureSignal())
            ps->setEnabled(on);
        if (!on && m_twoToneKeyed)
            requestTwoTone(false);
        refresh();
    });
    connect(m_autoBtn, &QPushButton::clicked, this, [this] {
        if (auto* ps = pureSignal())
            ps->setMode(anan::AnanPureSignal::Mode::Auto);
        refresh();
    });
    connect(m_singleBtn, &QPushButton::clicked, this, [this] {
        if (auto* ps = pureSignal())
            ps->setMode(anan::AnanPureSignal::Mode::Single);
        refresh();
    });
    connect(m_calBtn, &QPushButton::clicked, this, [this] {
        if (auto* ps = pureSignal()) {
            ps->setMode(anan::AnanPureSignal::Mode::Single);
            ps->calibrate();
        }
        refresh();
    });
    connect(m_resetBtn, &QPushButton::clicked, this, [this] {
        if (auto* ps = pureSignal())
            ps->resetCorrection();
    });
    connect(m_twoToneBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        requestTwoTone(on);
    });
    connect(m_ampViewBtn, &QPushButton::clicked, this, &PsApplet::openAmpView);
    connect(m_source, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (!m_loading)
            if (auto* ps = pureSignal())
                ps->setFeedbackSource(i);
    });
    connect(m_fbAdc, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (!m_loading)
            if (auto* ps = pureSignal())
                ps->setFeedbackAdc(i);
    });
    connect(m_att, &QSlider::valueChanged, this, [this](int v) {
        m_attValue->setText(tr("%1 dB").arg(v));
        if (!m_loading)
            if (auto* ps = pureSignal())
                ps->setAttenuation(v);
    });
    connect(m_autoAtt, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_loading)
            if (auto* ps = pureSignal())
                ps->setAutoAttenuate(on);
    });
    m_timer = new QTimer(this);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, &PsApplet::refresh);
    m_timer->start();
    refresh();
}

void PsApplet::setRadioModel(RadioModel* model)
{
    m_model = model;
    refresh();
}

anan::AnanPureSignal* PsApplet::pureSignal() const
{
    if (!m_model || !m_model->isConnected())
        return nullptr;
    auto* be = dynamic_cast<hl2::Hl2Backend*>(m_model->backend());
    return be ? be->pureSignal() : nullptr;
}

bool PsApplet::twoToneAllowed() const
{
    if (!m_model)
        return false;
    SliceModel* s = m_model->txSlice();
    if (!s)
        return false;
    const QString mode = s->mode().toUpper();
    return mode == QLatin1String("USB") || mode == QLatin1String("LSB");
}

void PsApplet::requestTwoTone(bool on)
{
    anan::AnanPureSignal* ps = pureSignal();
    if (on) {
        if (!ps || !twoToneAllowed() || m_twoToneKeyed) {
            const QSignalBlocker b(m_twoToneBtn);
            m_twoToneBtn->setChecked(m_twoToneKeyed);
            return;
        }
        ps->setTwoTone(true);
        m_twoToneKeyed = true;
        m_sawTx = false;
        m_twoToneSince.start();
        if (m_mox)
            m_mox(true);
    } else {
        if (m_twoToneKeyed && m_mox)
            m_mox(false);
        if (ps)
            ps->setTwoTone(false);
        m_twoToneKeyed = false;
    }
    const QSignalBlocker b(m_twoToneBtn);
    m_twoToneBtn->setChecked(m_twoToneKeyed);
    if (m_ampView)
        m_ampView->syncTwoTone(m_twoToneKeyed);
}

void PsApplet::openAmpView()
{
    if (!m_ampView) {
        m_ampView = new PsAmpViewDialog(this, window());
        m_ampView->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_ampView->show();
    m_ampView->raise();
    m_ampView->activateWindow();
}

void PsApplet::loadControls()
{
    anan::AnanPureSignal* ps = pureSignal();
    if (!ps)
        return;
    const auto& s = ps->settings();
    m_loading = true;
    m_psBtn->setChecked(s.enabled);
    m_source->setCurrentIndex(s.feedbackSource);
    m_fbAdc->setCurrentIndex(s.feedbackAdc);
    m_att->setValue(s.attDb);
    m_attValue->setText(tr("%1 dB").arg(s.attDb));
    m_autoAtt->setChecked(s.autoAttenuate);
    m_loading = false;
}

void PsApplet::refresh()
{
    anan::AnanPureSignal* ps = pureSignal();
    if (ps != m_lastPs) {
        m_lastPs = ps;
        if (ps) {
            loadControls();
            connect(ps, &anan::AnanPureSignal::settingsChanged, this, &PsApplet::loadControls,
                    Qt::UniqueConnection);
        }
        if (!ps && m_twoToneKeyed)
            m_twoToneKeyed = false;
    }
    const bool have = ps != nullptr;
    for (QWidget* w : {static_cast<QWidget*>(m_psBtn), static_cast<QWidget*>(m_autoBtn),
                       static_cast<QWidget*>(m_singleBtn),
                       static_cast<QWidget*>(m_source), static_cast<QWidget*>(m_fbAdc),
                       static_cast<QWidget*>(m_att),
                       static_cast<QWidget*>(m_autoAtt),
                       static_cast<QWidget*>(m_ampViewBtn)})
        w->setEnabled(have);
    // Input switches the Alex relays in front of ADC1 only; ADC2 is the bare
    // RX2 input.
    m_source->setEnabled(have && ps->settings().feedbackAdc == 0);

    const bool allowed = have && twoToneAllowed();
    m_twoToneBtn->setEnabled(allowed || m_twoToneKeyed);
    m_twoToneBtn->setToolTip(allowed || m_twoToneKeyed
                                 ? tr("Transmit a two-tone test signal (SSB only).\n"
                                      "PureSignal calibrates on it; click again to stop.")
                                 : tr("2 Tone works in USB / LSB only"));

    // 2 TONE follows the transmitter: stopped elsewhere, or the mode left SSB.
    if (m_twoToneKeyed && m_model) {
        const bool tx = m_model->transmitModel().isTransmitting();
        if (tx)
            m_sawTx = true;
        const bool refused = !m_sawTx && m_twoToneSince.elapsed() > 3000;
        if ((m_sawTx && !tx) || !allowed || refused)
            requestTwoTone(false);
    }

    if (!ps) {
        setLamp(m_feedLamp, QStringLiteral("#1a2028"), QStringLiteral("#607080"));
        setLamp(m_fbLamp, QStringLiteral("#1a2028"), QStringLiteral("#607080"));
        setLamp(m_corrLamp, QStringLiteral("#1a2028"), QStringLiteral("#607080"));
        m_fbLamp->setText(tr("FB"));
        m_stateLabel->setText(tr("No radio"));
        m_calLabel->clear();
        m_calBtn->setEnabled(false);
        m_resetBtn->setEnabled(false);
        return;
    }
    const auto st = ps->status();
    m_calBtn->setEnabled(st.enabled);
    m_resetBtn->setEnabled(st.enabled);
    {
        const QSignalBlocker b(m_psBtn);
        m_psBtn->setChecked(st.enabled);
        const bool single = ps->settings().mode == anan::AnanPureSignal::Mode::Single;
        const QSignalBlocker b1(m_autoBtn), b2(m_singleBtn), b3(m_calBtn);
        m_autoBtn->setChecked(!single);
        m_singleBtn->setChecked(single);
        m_calBtn->setChecked(st.enabled && st.calibrating);
    }
    {
        const QSignalBlocker b(m_att);
        if (m_att->value() != st.attDb) {
            m_att->setValue(st.attDb);
            m_attValue->setText(tr("%1 dB").arg(st.attDb));
        }
    }
    const QString off = QStringLiteral("#1a2028");
    setLamp(m_feedLamp, st.feeding ? QStringLiteral("#30c060") : off,
            st.feeding ? QStringLiteral("#0b0e12") : QStringLiteral("#607080"));
    const int fb = st.feedbackLevel;
    QString fbColor = off;
    if (st.enabled && fb > 0) {
        if (fb > 181)
            fbColor = QStringLiteral("#3090ff");
        else if (fb > 128)
            fbColor = QStringLiteral("#30e030");
        else if (fb > 90)
            fbColor = QStringLiteral("#e0d020");
        else
            fbColor = QStringLiteral("#e03030");
    }
    setLamp(m_fbLamp, fbColor, fbColor == off ? QStringLiteral("#607080") : QStringLiteral("#0b0e12"));
    m_fbLamp->setText(st.enabled && fb > 0 ? QStringLiteral("FB %1").arg(fb) : tr("FB"));
    setLamp(m_corrLamp, st.enabled && st.correcting ? QStringLiteral("#30e030") : off,
            st.enabled && st.correcting ? QStringLiteral("#0b0e12") : QStringLiteral("#607080"));
    const int si = std::clamp(st.state, 0, 9);
    m_stateLabel->setText(st.enabled ? tr("State: %1").arg(QLatin1String(kStateNames[si]))
                                     : tr("PureSignal off"));
    m_calLabel->setText(st.enabled ? tr("Cal %1").arg(st.calibrations) : QString());
}

} // namespace AetherSDR
