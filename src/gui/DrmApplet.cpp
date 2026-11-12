#include "DrmApplet.h"

#include "core/AppSettings.h"
#include "core/ThemeManager.h"
#include "gui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace AetherSDR {

namespace {

const char* kHeaderStyle =
    "QLabel { color: {{color.text.label}}; font-size: 10px; font-weight: bold; }";
const char* kDimLabel =
    "QLabel { color: {{color.text.secondary}}; font-size: 11px; }";
const char* kValueLabel =
    "QLabel { color: {{color.text.primary}}; font-size: 11px; }";
const char* kStationLabel =
    "QLabel { color: {{color.text.primary}}; font-size: 14px; font-weight: bold; }";
const char* kHintLabel =
    "QLabel { color: {{color.text.label}}; font-size: 10px; font-style: italic; }";
const char* kComboStyle =
    "QComboBox { font-size: 11px; background: {{color.background.1}}; border: 1px solid {{color.border.subtle}};"
    " border-radius: 3px; padding: 0px 3px; color: {{color.text.primary}}; }"
    "QComboBox::drop-down { border: none; }"
    "QComboBox QAbstractItemView { background: {{color.background.1}}; color: {{color.text.primary}}; }";
const char* kFrameStyle =
    "QFrame#drmSlideFrame { background: {{color.background.0}}; border: 1px solid {{color.border.subtle}};"
    " border-radius: 3px; }";
const char* kTextStyle =
    "QPlainTextEdit { background: {{color.background.0}}; color: {{color.text.primary}};"
    " border: 1px solid {{color.border.subtle}}; border-radius: 3px; font-size: 11px; }";

const char* const kLightNames[6] = {"IO", "Time", "Freq", "FAC", "SDC", "MSC"};
const char* const kLightTips[6] = {
    "Samples from the receiver",
    "Time synchronisation (guard interval)",
    "Frequency synchronisation",
    "FAC: the channel description (mode, bandwidth, services)",
    "SDC: the service description (labels, codecs)",
    "MSC: audio and data frames"};

// Fixed status colours (the same in every theme, like a front-panel LED).
QString lightStyle(DrmStatus::Light l)
{
    const char* bg = "#3a3f44";            // off
    const char* fg = "#8a9096";
    switch (l) {
    case DrmStatus::Light::Ok:     bg = "#1f8f3a"; fg = "#ffffff"; break;
    case DrmStatus::Light::Error:  bg = "#b3261e"; fg = "#ffffff"; break;
    case DrmStatus::Light::Absent: bg = "#8a6d1a"; fg = "#ffffff"; break;
    case DrmStatus::Light::Off:    break;
    }
    return QStringLiteral("QLabel { background: %1; color: %2; border-radius: 3px;"
                          " font-size: 10px; font-weight: bold; padding: 1px 0px; }")
        .arg(QLatin1String(bg), QLatin1String(fg));
}

QLabel* makeLabel(const QString& text, const char* style)
{
    auto* l = new QLabel(text);
    ThemeManager::instance().applyStyleSheet(l, QString::fromLatin1(style));
    return l;
}

} // namespace

DrmApplet::DrmApplet(QWidget* parent)
    : QWidget(parent)
{
    loadSettings();
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);
    buildCompact(root);
    buildLarge(root);
    m_large->setVisible(false);
    refresh();
}

void DrmApplet::buildCompact(QVBoxLayout* root)
{
    // Header: enable, receiver
    auto* head = new QHBoxLayout;
    head->setSpacing(6);
    m_enableBtn = new QPushButton(m_enabled ? QStringLiteral("DRM On") : QStringLiteral("DRM Off"));
    m_enableBtn->setCheckable(true);
    m_enableBtn->setChecked(m_enabled);
    m_enableBtn->setFixedHeight(22);
    m_enableBtn->setToolTip(QStringLiteral(
        "Decode DRM on the selected receiver. Tune the receiver to the centre of the\n"
        "DRM signal; the mode is set to DRM while the decoder runs."));
    applyToggleButtonStyle(m_enableBtn, ToggleTribe::Success);
    head->addWidget(m_enableBtn);
    head->addWidget(makeLabel(QStringLiteral("Slice"), kDimLabel));
    m_sliceCombo = new QComboBox;
    m_sliceCombo->setFixedHeight(20);
    m_sliceCombo->addItem(QStringLiteral("A"));
    ThemeManager::instance().applyStyleSheet(m_sliceCombo, QString::fromLatin1(kComboStyle));
    head->addWidget(m_sliceCombo);
    head->addStretch(1);
    root->addLayout(head);

    connect(m_enableBtn, &QPushButton::toggled, this, [this](bool on) {
        setEnabledUi(on);
        saveSettings();
        emit enableChanged(on);
    });
    connect(m_sliceCombo, &QComboBox::currentTextChanged, this, [this](const QString& s) {
        if (!s.isEmpty())
            emit sliceChanged(s);
    });

    // Status lights
    auto* lights = new QHBoxLayout;
    lights->setSpacing(3);
    for (int i = 0; i < 6; ++i) {
        m_lights[i] = new QLabel(QLatin1String(kLightNames[i]));
        m_lights[i]->setAlignment(Qt::AlignCenter);
        m_lights[i]->setMinimumWidth(30);
        m_lights[i]->setFixedHeight(16);   // build 305
        m_lights[i]->setToolTip(QLatin1String(kLightTips[i]));
        lights->addWidget(m_lights[i], 1);
    }
    root->addLayout(lights);

    // Signal line: SNR | mode, bandwidth, QAM
    auto* sig = new QHBoxLayout;
    sig->setSpacing(8);
    m_snr = makeLabel(QString(), kValueLabel);
    m_mode = makeLabel(QString(), kDimLabel);
    // Build 305 (operator, 11.11: "when FAC turns green the window redraws
    // and jumps"): no row may change the applet's size as the decoder's
    // state comes and goes. Text widths do not count (Ignored), and every
    // row has a fixed height whether it holds text or not.
    for (QLabel* l : {m_snr, m_mode}) {
        l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        l->setFixedHeight(16);
    }
    m_snr->setMinimumWidth(80);
    sig->addWidget(m_snr);
    sig->addWidget(m_mode, 1);
    root->addLayout(sig);

    // Station
    // Build 304 (operator, 11.11: "the station name TDF-DRM shows now and
    // then and the window grows down and back -- give the name its place at
    // once"). The row is always there, one line high (14 px bold), empty
    // while no label is known. DRM labels are at most 16 characters, so one
    // line, no wrap.
    m_label = makeLabel(QString(), kStationLabel);
    m_label->setWordWrap(false);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_label->setFixedHeight(22);
    root->addWidget(m_label);

    // Services 1..4
    auto* svc = new QHBoxLayout;
    svc->setSpacing(3);
    for (int i = 0; i < 4; ++i) {
        m_serviceBtns[i] = new QPushButton(QString::number(i + 1));
        m_serviceBtns[i]->setCheckable(true);
        m_serviceBtns[i]->setFixedHeight(20);
        applyToggleButtonStyle(m_serviceBtns[i]);
        connect(m_serviceBtns[i], &QPushButton::clicked, this, [this, i] {
            emit serviceSelected(i);
        });
        svc->addWidget(m_serviceBtns[i], 1);
    }
    root->addLayout(svc);
    // Build 305: fixed rows -- the service line one line, the text message
    // three (DRM text messages are up to 128 characters), the note two.
    m_serviceInfo = makeLabel(QString(), kDimLabel);
    m_serviceInfo->setWordWrap(false);
    m_serviceInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_serviceInfo->setFixedHeight(16);
    root->addWidget(m_serviceInfo);

    // Text message
    m_text = makeLabel(QString(), kValueLabel);
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_text->setFixedHeight(46);
    root->addWidget(m_text);

    m_note = makeLabel(QString(), kHintLabel);
    m_note->setWordWrap(true);
    m_note->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_note->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_note->setFixedHeight(30);
    root->addWidget(m_note);
}

void DrmApplet::buildLarge(QVBoxLayout* root)
{
    m_large = new QFrame;
    auto* vb = new QVBoxLayout(m_large);
    vb->setContentsMargins(0, 4, 0, 0);
    vb->setSpacing(4);

    // Picture (MOT slideshow)
    vb->addWidget(makeLabel(QStringLiteral("SLIDESHOW"), kHeaderStyle));
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("drmSlideFrame"));
    ThemeManager::instance().applyStyleSheet(frame, QString::fromLatin1(kFrameStyle));
    auto* fv = new QVBoxLayout(frame);
    fv->setContentsMargins(2, 2, 2, 2);
    m_slide = new QLabel;
    m_slide->setAlignment(Qt::AlignCenter);
    m_slide->setMinimumSize(320, 240);
    m_slide->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    fv->addWidget(m_slide);
    vb->addWidget(frame, 3);
    m_slideName = makeLabel(QString(), kDimLabel);
    vb->addWidget(m_slideName);

    // Journaline: double-click a line with a [number] to open that page
    auto* jhead = new QHBoxLayout;
    jhead->addWidget(makeLabel(QStringLiteral("JOURNALINE"), kHeaderStyle));
    jhead->addStretch(1);
    m_journalineHome = new QPushButton(QStringLiteral("Menu"));
    m_journalineHome->setFixedHeight(18);
    m_journalineHome->setToolTip(QStringLiteral("Back to the Journaline main menu"));
    applyToggleButtonStyle(m_journalineHome);
    jhead->addWidget(m_journalineHome);
    vb->addLayout(jhead);
    m_journaline = new QPlainTextEdit;
    m_journaline->setReadOnly(true);
    m_journaline->setMinimumHeight(100);
    m_journaline->setToolTip(QStringLiteral("Double-click a line marked [number] to open it"));
    m_journaline->viewport()->installEventFilter(this);
    ThemeManager::instance().applyStyleSheet(m_journaline, QString::fromLatin1(kTextStyle));
    vb->addWidget(m_journaline, 2);
    connect(m_journalineHome, &QPushButton::clicked, this, [this] { emit journalinePageRequested(0); });

    // Channel details
    vb->addWidget(makeLabel(QStringLiteral("CHANNEL"), kHeaderStyle));
    m_details = makeLabel(QString(), kDimLabel);
    m_details->setWordWrap(true);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    vb->addWidget(m_details);

    // Receiver settings
    vb->addWidget(makeLabel(QStringLiteral("RECEIVER"), kHeaderStyle));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(3);
    m_freqIntCombo = new QComboBox;
    m_freqIntCombo->addItems({QStringLiteral("Linear"), QStringLiteral("Wiener"), QStringLiteral("DFT")});
    m_freqIntCombo->setCurrentIndex(m_settings.freqInterpolation);
    m_freqIntCombo->setToolTip(QStringLiteral("Channel estimation across the carriers. Wiener is the best on fading HF."));
    m_timeIntCombo = new QComboBox;
    m_timeIntCombo->addItems({QStringLiteral("Linear"), QStringLiteral("Wiener")});
    m_timeIntCombo->setCurrentIndex(m_settings.timeInterpolation);
    m_timeIntCombo->setToolTip(QStringLiteral("Channel estimation over time."));
    m_mlcSpin = new QSpinBox;
    m_mlcSpin->setRange(0, 4);
    m_mlcSpin->setValue(m_settings.mlcIterations);
    m_mlcSpin->setToolTip(QStringLiteral("Decoder iterations for the MSC. More can rescue a weak signal, at more CPU."));
    m_flipCheck = new QCheckBox(QStringLiteral("Flip spectrum"));
    m_flipCheck->setChecked(m_settings.flipSpectrum);
    m_flipCheck->setToolTip(QStringLiteral("For a receiver that delivers the spectrum inverted."));
    for (QComboBox* c : {m_freqIntCombo, m_timeIntCombo})
        ThemeManager::instance().applyStyleSheet(c, QString::fromLatin1(kComboStyle));
    grid->addWidget(makeLabel(QStringLiteral("Frequency interpolation"), kDimLabel), 0, 0);
    grid->addWidget(m_freqIntCombo, 0, 1);
    grid->addWidget(makeLabel(QStringLiteral("Time interpolation"), kDimLabel), 1, 0);
    grid->addWidget(m_timeIntCombo, 1, 1);
    grid->addWidget(makeLabel(QStringLiteral("MSC decoder iterations"), kDimLabel), 2, 0);
    grid->addWidget(m_mlcSpin, 2, 1);
    grid->addWidget(m_flipCheck, 3, 0, 1, 2);
    grid->setColumnStretch(2, 1);
    vb->addLayout(grid);

    const auto changed = [this] {
        m_settings.freqInterpolation = m_freqIntCombo->currentIndex();
        m_settings.timeInterpolation = m_timeIntCombo->currentIndex();
        m_settings.mlcIterations = m_mlcSpin->value();
        m_settings.flipSpectrum = m_flipCheck->isChecked();
        saveSettings();
        emit settingsChanged();
    };
    connect(m_freqIntCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_timeIntCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(m_mlcSpin, &QSpinBox::valueChanged, this, changed);
    connect(m_flipCheck, &QCheckBox::toggled, this, changed);

    root->addWidget(m_large, 1);
}

void DrmApplet::setFloating(bool on)
{
    m_floating = on;
    m_large->setVisible(on);
    refresh();
}

bool DrmApplet::eventFilter(QObject* watched, QEvent* event)
{
    if (m_journaline && watched == m_journaline->viewport()
        && event->type() == QEvent::MouseButtonDblClick) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QString line = m_journaline->cursorForPosition(me->pos()).block().text();
        static const QRegularExpression re(QStringLiteral("^\\[(\\d+)\\]"));
        const auto m = re.match(line);
        if (m.hasMatch())
            emit journalinePageRequested(m.captured(1).toInt());
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void DrmApplet::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_floating && !m_slideImage.isNull())
        refresh();   // rescale the picture to the new size
}

void DrmApplet::setSlide(const QImage& image, const QString& name)
{
    m_slideImage = image;
    m_slideImageName = name;
    refresh();
}

void DrmApplet::clearSlide()
{
    m_slideImage = QImage();
    m_slideImageName.clear();
    refresh();
}

QString DrmApplet::sliceLetter() const
{
    return m_sliceCombo->currentText();
}

void DrmApplet::setDecoderAvailable(bool on)
{
    m_decoderAvailable = on;
    m_enableBtn->setEnabled(on || m_enabled);
    refresh();
}

void DrmApplet::forceOff()
{
    const QSignalBlocker b(m_enableBtn);
    m_enableBtn->setChecked(false);
    setEnabledUi(false);
    m_status = DrmStatus();
    clearSlide();
}

void DrmApplet::setSliceLetters(const QStringList& letters)
{
    const QString current = m_sliceCombo->currentText();
    const QSignalBlocker b(m_sliceCombo);
    m_sliceCombo->clear();
    m_sliceCombo->addItems(letters.isEmpty() ? QStringList{QStringLiteral("A")} : letters);
    const int i = m_sliceCombo->findText(current);
    m_sliceCombo->setCurrentIndex(i >= 0 ? i : 0);
}

void DrmApplet::setStatus(const DrmStatus& status)
{
    m_status = status;
    refresh();
}

void DrmApplet::setEnabledUi(bool on)
{
    m_enabled = on;
    m_enableBtn->setText(on ? QStringLiteral("DRM On") : QStringLiteral("DRM Off"));
    refresh();
}

void DrmApplet::loadSettings()
{
    auto& s = AppSettings::instance();
    m_enabled = false;   // never starts on by itself: the mode would change
    m_settings.freqInterpolation = std::clamp(s.value(QStringLiteral("DrmFreqInt"), 1).toInt(), 0, 2);
    m_settings.timeInterpolation = std::clamp(s.value(QStringLiteral("DrmTimeInt"), 1).toInt(), 0, 1);
    m_settings.mlcIterations = std::clamp(s.value(QStringLiteral("DrmMlcIter"), 1).toInt(), 0, 4);
    m_settings.flipSpectrum = s.value(QStringLiteral("DrmFlipSpectrum"), QStringLiteral("False")).toString()
                              == QLatin1String("True");
}

void DrmApplet::saveSettings()
{
    auto& s = AppSettings::instance();
    s.setValue(QStringLiteral("DrmFreqInt"), m_settings.freqInterpolation);
    s.setValue(QStringLiteral("DrmTimeInt"), m_settings.timeInterpolation);
    s.setValue(QStringLiteral("DrmMlcIter"), m_settings.mlcIterations);
    s.setValue(QStringLiteral("DrmFlipSpectrum"),
               m_settings.flipSpectrum ? QStringLiteral("True") : QStringLiteral("False"));
    s.save();
}

void DrmApplet::refresh()
{
    const DrmStatus& st = m_status;
    const bool live = m_enabled && st.running;

    const DrmStatus::Light lights[6] = {st.io, st.timeSync, st.freqSync, st.fac, st.sdc, st.msc};
    // Build 305: restyled only when a light changes (a style sheet set every
    // 250 ms re-polishes the label each time).
    for (int i = 0; i < 6; ++i) {
        const QString style = lightStyle(live ? lights[i] : DrmStatus::Light::Off);
        if (m_lights[i]->styleSheet() != style)
            m_lights[i]->setStyleSheet(style);
    }

    if (live && st.robustness.isLetterOrNumber()) {
        m_snr->setText(QStringLiteral("SNR %1 dB").arg(st.snrDb, 0, 'f', 1));
        QStringList parts;
        parts << QStringLiteral("Mode %1").arg(st.robustness);
        if (st.bandwidthKHz > 0)
            parts << QStringLiteral("%1 kHz").arg(st.bandwidthKHz, 0, 'g', 3);
        if (!st.mscQam.isEmpty())
            parts << st.mscQam;
        if (!st.interleaver.isEmpty())
            parts << st.interleaver;
        m_mode->setText(parts.join(QStringLiteral(" \u00B7 ")));
    } else {
        m_snr->setText(live ? QStringLiteral("SNR \u2014") : QString());
        m_mode->setText(QString());
    }

    // Station label: the selected service, else the first one present.
    const int sel = (st.selectedService >= 0 && st.selectedService < st.services.size())
        ? st.selectedService : -1;
    QString station;
    if (live) {
        if (sel >= 0)
            station = st.services[sel].label;
        for (int i = 0; station.isEmpty() && i < st.services.size(); ++i)
            if (st.services[i].present)
                station = st.services[i].label;
    }
    m_label->setText(station);   // build 304: the row stays (no setVisible)

    for (int i = 0; i < 4; ++i) {
        const bool present = live && i < st.services.size() && st.services[i].present;
        QPushButton* b = m_serviceBtns[i];
        const QSignalBlocker blk(b);
        b->setEnabled(present);
        b->setChecked(present && i == sel);
        if (present) {
            const auto& s = st.services[i];
            b->setText(QStringLiteral("%1 %2").arg(i + 1).arg(s.audio ? QStringLiteral("\u266A") : QStringLiteral("\u25A3")));
            b->setToolTip(QStringLiteral("%1\n%2%3").arg(s.label, s.codec,
                s.bitrateKbps > 0 ? QStringLiteral(", %1 kbit/s").arg(s.bitrateKbps, 0, 'f', 2) : QString()));
        } else {
            b->setText(QString::number(i + 1));
            b->setToolTip(QString());
        }
    }
    if (live && sel >= 0) {
        const auto& s = st.services[sel];
        QStringList info;
        info << s.codec;
        if (s.bitrateKbps > 0)
            info << QStringLiteral("%1 kbit/s").arg(s.bitrateKbps, 0, 'f', 2);
        if (!s.language.isEmpty())
            info << s.language;
        if (!s.country.isEmpty())
            info << s.country;
        if (!s.decodable)
            info << QStringLiteral("no decoder for this codec");
        m_serviceInfo->setText(info.join(QStringLiteral(" \u00B7 ")));
    } else {
        m_serviceInfo->setText(QString());
    }

    m_text->setText(live ? st.textMessage : QString());   // build 305: the row stays

    QString note;
    if (!m_decoderAvailable)
        note = QStringLiteral("DRM decoding needs an ANAN radio connected.");
    else if (!m_enabled)
        note = QStringLiteral("Tune a receiver to the centre of a DRM station and press DRM On.");
    else
        note = st.note;
    m_note->setText(note);   // build 305: the row stays

    if (!m_floating)
        return;
    if (m_enabled && !m_slideImage.isNull()) {
        const QSize box = m_slide->size().expandedTo(QSize(320, 240)) - QSize(4, 4);
        m_slide->setPixmap(QPixmap::fromImage(m_slideImage).scaled(box, Qt::KeepAspectRatio,
                                                                   Qt::SmoothTransformation));
        m_slideName->setText(m_slideImageName);
    } else {
        m_slide->setPixmap(QPixmap());
        m_slide->setText(live ? QStringLiteral("No picture received yet") : QString());
        m_slideName->setText(QString());
    }
    if (m_journaline->toPlainText() != (live ? st.journaline : QString()))
        m_journaline->setPlainText(live ? st.journaline : QString());

    QStringList d;
    if (live) {
        if (st.robustness.isLetterOrNumber())
            d << QStringLiteral("Robustness mode %1").arg(st.robustness);
        if (st.bandwidthKHz > 0)
            d << QStringLiteral("Bandwidth %1 kHz").arg(st.bandwidthKHz, 0, 'g', 3);
        if (!st.mscQam.isEmpty())
            d << QStringLiteral("MSC %1").arg(st.mscQam);
        if (!st.sdcQam.isEmpty())
            d << QStringLiteral("SDC %1").arg(st.sdcQam);
        if (!st.interleaver.isEmpty())
            d << QStringLiteral("Interleaver %1").arg(st.interleaver);
        d << QStringLiteral("Frequency offset %1 Hz").arg(st.freqOffsetHz, 0, 'f', 1);
        if (!st.stationTime.isEmpty())
            d << QStringLiteral("Time %1").arg(st.stationTime);
    }
    m_details->setText(d.join(QStringLiteral("\n")));
}

} // namespace AetherSDR
