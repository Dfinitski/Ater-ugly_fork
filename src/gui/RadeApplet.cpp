#ifdef HAVE_RADE

#include "RadeApplet.h"
#include "ComboStyle.h"
#include "GuardedSlider.h"
#include "core/ThemeManager.h"
#include "core/fdv/FdvState.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
// Base style for muted-grey informational labels (SNR value, offset, "SNR" caption).
// Signal-specific colors (#e0e040 / #00ff88) are substituted into this template
// by setRadeSnr; all other uses take the grey directly.
constexpr auto kMutedStyle =
    "QLabel { color: #8090a0; font-size: 10px;"
    " background: transparent; border: none; padding: 0; margin: 0; }";
} // namespace

namespace AetherSDR {

RadeApplet::RadeApplet(QWidget* parent)
    : QWidget(parent)
{
    theme::setContainer(this, QStringLiteral("applet/rade"));

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(2);

    // ---- Settings: sub-mode and callsign ----
    {
        auto& fs = fdv::FdvState::instance();
        const QString fieldStyle = QStringLiteral(
            "QLineEdit { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px;"
            " color: #c8d8e8; font-size: 11px; font-weight: bold; padding: 0px 4px; }"
            "QLineEdit:focus { border: 1px solid #0090e0; }");
        auto* modeRow = new QHBoxLayout;
        modeRow->setContentsMargins(0, 0, 0, 0);
        modeRow->setSpacing(4);
        auto* modeLbl = new QLabel(tr("Mode:"));
        modeLbl->setStyleSheet(kMutedStyle);
        modeLbl->setFixedWidth(34);
        modeRow->addWidget(modeLbl);
        m_modeCombo = new GuardedComboBox;
        m_modeCombo->setObjectName(QStringLiteral("fdvModeCombo"));
        m_modeCombo->setAccessibleName(tr("FreeDV mode"));
        m_modeCombo->setFixedHeight(20);
        for (int m = 0; m < fdv::kFdvSubModeCount; ++m)
            m_modeCombo->addItem(QLatin1String(fdv::fdvSubModeName(m)));
        m_modeCombo->setCurrentIndex(fs.subMode);
        AetherSDR::applyComboStyle(m_modeCombo);
        m_modeCombo->setToolTip(tr("FreeDV mode: 1600, 700D, 700E (codec2) or RADE V1"));
        connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int m) {
            auto& st = fdv::FdvState::instance();
            st.setSubMode(m);
            if (m_active && st.sliceId >= 0) {
                m_modeLabel = st.label();
                setRadeSynced(false);
            }
        });
        modeRow->addWidget(m_modeCombo, 1);
        vbox->addLayout(modeRow);

        auto* callRow = new QHBoxLayout;
        callRow->setContentsMargins(0, 0, 0, 0);
        callRow->setSpacing(4);
        auto* callLbl = new QLabel(tr("Call:"));
        callLbl->setStyleSheet(kMutedStyle);
        callLbl->setFixedWidth(34);
        callRow->addWidget(callLbl);
        m_callsignEdit = new QLineEdit(fs.callsign);
        m_callsignEdit->setObjectName(QStringLiteral("fdvCallsignEdit"));
        m_callsignEdit->setAccessibleName(tr("FreeDV callsign"));
        m_callsignEdit->setFixedHeight(20);
        m_callsignEdit->setMaxLength(8);
        m_callsignEdit->setStyleSheet(fieldStyle);
        m_callsignEdit->setPlaceholderText(tr("CALLSIGN"));
        m_callsignEdit->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[A-Z0-9]{0,8}")), m_callsignEdit));
        m_callsignEdit->setToolTip(tr("Callsign sent with FreeDV: up to 8 characters, A-Z and 0-9.\n"
                                      "RADE: at the end of each over; 1600 / 700D / 700E: on the text channel"));
        // Lower case is accepted as typed and shown upper case (the validator
        // alone would refuse it).
        connect(m_callsignEdit, &QLineEdit::textEdited, this, [this](const QString& t) {
            const QString clean = fdv::FdvState::sanitizeCallsign(t);
            if (clean != t) {
                const int pos = m_callsignEdit->cursorPosition();
                m_callsignEdit->setText(clean);
                m_callsignEdit->setCursorPosition(std::min(pos, static_cast<int>(clean.size())));
            }
            fdv::FdvState::instance().setCallsign(clean);
        });
        connect(m_callsignEdit, &QLineEdit::returnPressed, m_callsignEdit, &QLineEdit::clearFocus);
        callRow->addWidget(m_callsignEdit, 1);
        vbox->addLayout(callRow);

        m_errorLabel = new QLabel;
        m_errorLabel->setWordWrap(true);
        m_errorLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: #e05050; font-size: 10px; background: transparent; border: none; }"));
        m_errorLabel->hide();
        vbox->addWidget(m_errorLabel);

        auto* poll = new QTimer(this);
        poll->setInterval(200);
        connect(poll, &QTimer::timeout, this, [this] { pollLocalFdv(); });
        poll->start();
    }

    m_inactiveLabel = new QLabel(tr("FDV inactive"));
    m_inactiveLabel->setStyleSheet(
        "QLabel { color: #506070; font-size: 10px; "
        "background: transparent; border: none; }");
    m_inactiveLabel->setAlignment(Qt::AlignCenter);
    vbox->addWidget(m_inactiveLabel);

    m_dataRows = new QWidget;
    m_dataRows->setAttribute(Qt::WA_TranslucentBackground);
    auto* dataVbox = new QVBoxLayout(m_dataRows);
    dataVbox->setContentsMargins(0, 0, 0, 0);
    dataVbox->setSpacing(3);

    m_statusLabel = new QLabel;
    m_statusLabel->setTextFormat(Qt::RichText);
    ThemeManager::instance().applyStyleSheet(m_statusLabel,
        "QLabel { color: {{color.accent}}; font-size: 10px; font-weight: bold;"
        " background: transparent; border: none; padding: 0; margin: 0; }");
    dataVbox->addWidget(m_statusLabel);

    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);

        auto* snrLbl = new QLabel(tr("SNR"));
        snrLbl->setStyleSheet(kMutedStyle);

        m_snrLabel = new QLabel(QStringLiteral("---"));
        m_snrLabel->setStyleSheet(kMutedStyle);

        row->addWidget(snrLbl);
        row->addWidget(m_snrLabel);
        row->addStretch();
        dataVbox->addLayout(row);
    }

    m_callsignLabel = new QLabel;
    ThemeManager::instance().applyStyleSheet(m_callsignLabel,
        "QLabel { color: {{color.accent}}; font-size: 10px; font-weight: bold;"
        " background: transparent; border: none; padding: 0; margin: 0; }");
    m_callsignLabel->hide();
    dataVbox->addWidget(m_callsignLabel);

    m_offsetLabel = new QLabel;
    m_offsetLabel->setStyleSheet(kMutedStyle);
    m_offsetLabel->hide();
    dataVbox->addWidget(m_offsetLabel);

    dataVbox->addStretch();
    m_dataRows->hide();
    vbox->addWidget(m_dataRows);
    vbox->addStretch();
}

void RadeApplet::setRadeActive(bool on, const QString& label)
{
    m_active = on;
    m_modeLabel = label.isEmpty() ? QStringLiteral("FDV") : label;

    m_inactiveLabel->setVisible(!on);
    m_dataRows->setVisible(on);

    if (!on) {
        m_statusLabel->setText({});
        m_snrLabel->setText(QStringLiteral("---"));
        m_snrLabel->setStyleSheet(kMutedStyle);
        m_callsignLabel->hide();
        m_callsignLabel->clear();
        m_offsetLabel->hide();
    } else {
        // Show initial unsynced state — syncChanged fires once audio starts flowing
        const QString led = QStringLiteral("<font color='#505050'>○</font>");
        m_statusLabel->setText(m_modeLabel + QLatin1Char(' ') + led);
        m_callsignLabel->clear();
        m_callsignLabel->hide();
        m_offsetLabel->hide();
    }
}

void RadeApplet::pollLocalFdv()
{
    const auto& st = fdv::FdvState::instance();
    // Error from the engine (libfdv missing) shown whether running or not.
    const bool err = !st.engineError.isEmpty();
    if (m_errorLabel->isVisible() != err || (err && m_errorLabel->text() != st.engineError)) {
        m_errorLabel->setText(st.engineError);
        m_errorLabel->setVisible(err);
    }
    if (st.sliceId < 0 || !m_active)
        return;
    const QString label = st.label();
    if (label != m_modeLabel) {
        m_modeLabel = label;
        setRadeSynced(st.sync);
    }
    if (st.sync != m_lastSync) {
        m_lastSync = st.sync;
        setRadeSynced(st.sync);
    }
    if (st.sync) {
        setRadeSnr(st.snrDb);
        if (st.subMode == static_cast<int>(fdv::FdvSubMode::RADE))
            setRadeFreqOffset(st.freqOffsetHz);
    }
    if (st.rxCallsignSerial != m_lastCallSerial) {
        if (m_lastCallSerial >= 0 || !st.rxCallsign.isEmpty())
            setRadeCallsign(st.rxCallsign);
        m_lastCallSerial = st.rxCallsignSerial;
    }
}

void RadeApplet::setRadeSynced(bool synced)
{
    if (!m_active) return;

    const QString led = synced
        ? QStringLiteral("<font color='#00ff88'>●</font>")
        : QStringLiteral("<font color='#505050'>○</font>");
    m_statusLabel->setText(m_modeLabel + QLatin1Char(' ') + led);

    if (!synced) {
        m_snrLabel->setStyleSheet(kMutedStyle);
        m_snrLabel->setText(QStringLiteral("---"));
        m_offsetLabel->hide();
    } else {
        // New sync — clear callsign from previous transmission
        m_callsignLabel->clear();
        m_callsignLabel->hide();
    }
}

void RadeApplet::setRadeSnr(float snrDb)
{
    if (!m_active) return;
    const QString color = (snrDb < 5.0f) ? QStringLiteral("#e0e040")
                                          : QStringLiteral("#00ff88");
    m_snrLabel->setStyleSheet(
        QString("QLabel { color: %1; font-size: 10px;"
                " background: transparent; border: none; padding: 0; margin: 0; }")
            .arg(color));
    m_snrLabel->setText(QString("%1dB").arg(qRound(snrDb)));
}

void RadeApplet::setRadeFreqOffset(float hz)
{
    if (!m_active) return;
    const QString sign = (hz >= 0) ? QStringLiteral("+") : QString{};
    m_offsetLabel->setText(
        QString("%1%2Hz").arg(sign).arg(static_cast<int>(hz)));
    m_offsetLabel->show();
}

void RadeApplet::setRadeCallsign(const QString& callsign)
{
    if (callsign.isEmpty()) {
        m_callsignLabel->clear();
        m_callsignLabel->hide();
    } else {
        m_callsignLabel->setText(callsign);
        m_callsignLabel->show();
    }
}

} // namespace AetherSDR

#endif // HAVE_RADE
