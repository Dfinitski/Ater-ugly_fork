#include "BandStackPanel.h"
#include "core/BandStackSettings.h"
#include "models/BandPlanManager.h"
#include "models/BandDefs.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QMenu>
#include <QMap>
#include "core/ThemeManager.h"
#include "core/AppSettings.h"

#include <algorithm>

namespace AetherSDR {

static const char* kMenuStyle =
    "QMenu { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050; }"
    "QMenu::item:selected { background: #304050; }"
    "QMenu::item:disabled { color: #607080; }"
    "QMenu::separator { background: #304050; height: 1px; margin: 4px 8px; }";

BandStackPanel::BandStackPanel(QWidget* parent)
    : QWidget(parent)
{
    // Build 645: "Group by freq".
    m_groupByFreq = AppSettings::instance()
                        .value(QStringLiteral("BandStackGroupByFreq"), QStringLiteral("False"))
                        .toString() == QLatin1String("True");
    theme::setContainer(this, QStringLiteral("panel/bandStack"));
    setFixedWidth(80);
    // Build 320 (operator, 12.11: build 313/314 changed the colour of the
    // panel's elements, "only the tooltips were meant"). The panel's rule is
    // back to applying to the panel and everything in it, as before build
    // 313 (`* {}` is what the old selector-less sheet meant), so the elements
    // look as they did. A tooltip also matches this sheet, so it gets its own
    // QToolTip rule here -- the application standard of build 314 -- which,
    // being more specific than `*`, wins over the panel background.
    AetherSDR::ThemeManager::instance().applyStyleSheet(this,
        "* { background: {{color.background.0}}; }"
        "QToolTip { color: #ffffff; background-color: #000000;"
        " border: 1px solid #00b4d8; padding: 3px 5px; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // Scrollable area for bookmark buttons
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    AetherSDR::ThemeManager::instance().applyStyleSheet(m_scrollArea, "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: {{color.background.0}}; width: 6px; }"
        "QScrollBar::handle:vertical { background: {{color.background.2}}; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    auto* scrollContent = new QWidget;
    m_buttonLayout = new QVBoxLayout(scrollContent);
    // Build 269 (operator, 11.11): buttons 10 % lower (26 -> 23 px) with the
    // gap grown by the same 3 px, so each button keeps its place (pitch 29);
    // the 1 px top margin keeps their centres.
    m_buttonLayout->setContentsMargins(0, 1, 0, 0);
    m_buttonLayout->setSpacing(6);
    m_buttonLayout->addStretch(1);  // push buttons to top

    m_scrollArea->setWidget(scrollContent);
    root->addWidget(m_scrollArea, 1);

    // Bottom button row: [Clear All] [+] [Settings]
    auto* bottomRow = new QHBoxLayout;
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(2);

    static const char* kSmallBtnStyle =
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #8aa8c0; font-size: 13px; font-weight: bold; "
        "padding: 0px; margin: 0px; }"
        "QPushButton:hover { background: #203040; color: #c8d8e8; }";

    m_clearAllButton = new QPushButton(QString::fromUtf8("\xc3\x97"), this);  // ×
    m_clearAllButton->setFixedSize(22, 26);
    m_clearAllButton->setStyleSheet(kSmallBtnStyle);
    m_clearAllButton->setToolTip("Clear all bookmarks");
    connect(m_clearAllButton, &QPushButton::clicked,
            this, &BandStackPanel::clearAllRequested);
    bottomRow->addWidget(m_clearAllButton);

    m_addButton = new QPushButton("+", this);
    m_addButton->setFixedSize(26, 26);
    AetherSDR::ThemeManager::instance().applyStyleSheet(m_addButton, "QPushButton { background: {{color.background.1}}; border: 1px solid {{color.background.2}}; "
        "border-radius: 3px; color: {{color.text.secondary}}; font-size: 16px; font-weight: bold; "
        "padding: 0px; margin: 0px; }"
        "QPushButton:hover { background: {{color.background.1}}; color: {{color.text.primary}}; }");
    m_addButton->setToolTip("Save current frequency as a bookmark");
    connect(m_addButton, &QPushButton::clicked, this, &BandStackPanel::addRequested);
    bottomRow->addWidget(m_addButton);

    m_settingsButton = new QPushButton(QString::fromUtf8("\xe2\x9a\x99"), this);  // gear
    m_settingsButton->setFixedSize(22, 26);
    m_settingsButton->setStyleSheet(kSmallBtnStyle);
    m_settingsButton->setToolTip(
        "Band stack options.\n"
        "Pair Auto-save (dwell) with Auto-expiry for a self-pruning rolling history.");
    connect(m_settingsButton, &QPushButton::clicked, this, [this]() {
        QMenu menu;
        menu.setStyleSheet(kMenuStyle);

        auto* groupAction = menu.addAction("Group by band");
        groupAction->setCheckable(true);
        groupAction->setChecked(m_grouped);
        // Build 645.
        auto* freqAction = menu.addAction("Group by freq");
        freqAction->setCheckable(true);
        freqAction->setChecked(m_groupByFreq);

        menu.addSeparator();
        auto* expiryLabel = menu.addAction("Auto-expiry:");
        expiryLabel->setEnabled(false);

        const int expiryOpts[] = {0, 5, 15, 30, 60};
        const char* expiryNames[] = {"  Off", "  5 min", "  15 min", "  30 min", "  60 min"};
        QAction* expiryActions[5];
        for (int i = 0; i < 5; ++i) {
            expiryActions[i] = menu.addAction(expiryNames[i]);
            expiryActions[i]->setCheckable(true);
            expiryActions[i]->setChecked(m_autoExpiryMinutes == expiryOpts[i]);
        }

        menu.addSeparator();
        auto* dwellLabel = menu.addAction("Auto-save dwell:");
        dwellLabel->setEnabled(false);

        const int dwellOpts[] = {0, 10, 30, 60};
        const char* dwellNames[] = {"  Off", "  10 sec", "  30 sec", "  60 sec"};
        QAction* dwellActions[4];
        for (int i = 0; i < 4; ++i) {
            dwellActions[i] = menu.addAction(dwellNames[i]);
            dwellActions[i]->setCheckable(true);
            dwellActions[i]->setChecked(m_autoSaveDwellSeconds == dwellOpts[i]);
        }

        // Anchor popup at the button's bottom-left so the menu opens below it
        // rather than overlapping the button's top-left corner.
        QAction* result = menu.exec(
            m_settingsButton->mapToGlobal(m_settingsButton->rect().bottomLeft()));
        if (!result) return;

        if (result == groupAction) {
            m_grouped = !m_grouped;
            emit groupByBandChanged(m_grouped);
            rebuildLayout();
            return;
        }
        if (result == freqAction) {   // build 645
            m_groupByFreq = !m_groupByFreq;
            auto& st = AppSettings::instance();
            st.setValue(QStringLiteral("BandStackGroupByFreq"),
                        m_groupByFreq ? QStringLiteral("True") : QStringLiteral("False"));
            st.save();
            rebuildLayout();
            return;
        }
        for (int i = 0; i < 5; ++i) {
            if (result == expiryActions[i]) {
                m_autoExpiryMinutes = expiryOpts[i];
                emit autoExpiryChanged(expiryOpts[i]);
                return;
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (result == dwellActions[i]) {
                m_autoSaveDwellSeconds = dwellOpts[i];
                emit autoSaveDwellChanged(dwellOpts[i]);
                return;
            }
        }
    });
    bottomRow->addWidget(m_settingsButton);

    root->addLayout(bottomRow);
}

QColor BandStackPanel::colorForFrequency(double freqMhz, const BandPlanManager* bpm)
{
    if (!bpm) return QColor(0x30, 0x40, 0x50);  // dark grey default
    for (const auto& seg : bpm->segments()) {
        if (freqMhz >= seg.lowMhz && freqMhz <= seg.highMhz) {
            return seg.color;
        }
    }
    return QColor(0x30, 0x40, 0x50);
}

QString BandStackPanel::bandNameForFrequency(double freqMhz)
{
    for (int i = 0; i < kBandCount; ++i) {
        if (freqMhz >= kBands[i].lowMhz && freqMhz <= kBands[i].highMhz) {
            return QString(kBands[i].name);
        }
    }
    return "Other";
}

void BandStackPanel::loadBookmarks(const RadioSettingsScope& scope, const BandPlanManager* bpm)
{
    clear();
    const auto entries = BandStackSettings::instance().entries(scope);
    for (int i = 0; i < entries.size(); ++i) {
        const BandStackEntry& entry = entries[i];
        Bookmark bm;
        bm.entry = entry;
        bm.color = colorForFrequency(entry.frequencyMhz, bpm);
        bm.settingsIndex = i;
        m_bookmarks.append(bm);
    }
    rebuildLayout();
}

QLabel* BandStackPanel::createBandHeader(const QString& bandName,
                                         double lowMhz, double highMhz)
{
    auto* label = new QLabel(bandName, m_scrollArea->widget());
    label->setFixedHeight(18);
    label->setAlignment(Qt::AlignCenter);
    AetherSDR::ThemeManager::instance().applyStyleSheet(label, "QLabel { color: {{color.text.label}}; font-size: 10px; font-weight: bold; "
        "border-bottom: 1px solid {{color.background.1}}; padding-bottom: 1px; margin-top: 4px; }");

    if (lowMhz > 0 || highMhz > 0) {
        label->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(label, &QLabel::customContextMenuRequested, this,
                [this, bandName, lowMhz, highMhz, label](const QPoint&) {
            QMenu menu;
            menu.setStyleSheet(kMenuStyle);
            auto* clearAction = menu.addAction(
                QString("Clear %1").arg(bandName));
            if (menu.exec(label->mapToGlobal(QPoint(label->width(), 0)))
                    == clearAction) {
                emit clearBandRequested(lowMhz, highMhz);
            }
        });
    }

    return label;
}

void BandStackPanel::rebuildLayout()
{
    // Remove all items from layout without deleting bookmark buttons
    qDeleteAll(m_bandHeaders);
    m_bandHeaders.clear();

    while (QLayoutItem* item = m_buttonLayout->takeAt(0)) {
        // Widget items: don't delete the widget (buttons are tracked in m_bookmarks)
        delete item;
    }

    // Create buttons for each bookmark
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        auto& bm = m_bookmarks[i];
        if (bm.button) {
            bm.button->disconnect();
            continue;  // button already exists
        }

        auto* btn = new QPushButton(m_scrollArea->widget());
        btn->setFixedSize(72, 23);   // build 252: 32 -> 26; build 269: -10 % -> 23
        btn->setText(QString::number(bm.entry.frequencyMhz, 'f', 3));
        QString tip = QString("%1 MHz  %2  %3")
            .arg(bm.entry.frequencyMhz, 0, 'f', 6)
            .arg(bm.entry.mode)
            .arg(bm.entry.rxAntenna);
        // Build 269: the view the button restores.
        if (bm.entry.sampleRateHz > 0)
            tip += QString("\nRate %1 kHz").arg(bm.entry.sampleRateHz / 1000);
        if (bm.entry.panBandwidthMhz > 0.0)
            tip += QString("\nPan %1 MHz, span %2 kHz")
                       .arg(bm.entry.panCenterMhz, 0, 'f', 4)
                       .arg(bm.entry.panBandwidthMhz * 1000.0, 0, 'f', 1);
        btn->setToolTip(tip);

        // Build 761 (operator, 26.11: "the quick memory buttons, when orange,
        // are too bright" -- the muted option chosen): the band plan's colour
        // mixed into the panel's #1a2a3a (the theme's, via its literal remap)
        // -- fill 45 %, border 70 %, hover 60 %; text #f0f0f0. Was: fill the
        // colour 30 % darker, border and hover the colour itself, white text.
        // Build 281 (operator, 11.11): text one size larger, 11 -> 12 px.
        const QColor base = AetherSDR::ThemeManager::instance().remapLiteralColor(
            QColor(0x1a, 0x2a, 0x3a));
        const auto mixed = [&base](const QColor& c, double t) {
            return QColor::fromRgbF(float(c.redF() * t + base.redF() * (1.0 - t)),
                                    float(c.greenF() * t + base.greenF() * (1.0 - t)),
                                    float(c.blueF() * t + base.blueF() * (1.0 - t)));
        };
        // Build 767 (operator, 26.11: "the thin frame is still bright -- make
        // it more muted, for a little volume"): a slight top-to-bottom
        // gradient (48 % -> 34 % of the colour) and a bevel instead of the
        // bright frame -- top / left a shade lighter than the fill, bottom /
        // right the colour in near-black (20 %). Hover: the fill 10 % up.
        const QColor top = mixed(bm.color, 0.48), bottom = mixed(bm.color, 0.34);
        const QColor light = top.lighter(112);
        const QColor dark = QColor::fromRgbF(float(bm.color.redF() * 0.2 + 0.0196 * 0.8),
                                             float(bm.color.greenF() * 0.2 + 0.0314 * 0.8),
                                             float(bm.color.blueF() * 0.2 + 0.0471 * 0.8));
        btn->setStyleSheet(
            QString("QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                    "stop:0 %1, stop:1 %2); border: 1px solid; border-top-color: %3; "
                    "border-left-color: %3; border-bottom-color: %4; border-right-color: %4; "
                    "border-radius: 3px; color: #f0f0f0; font-size: 12px; font-weight: bold; }"
                    "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                    "stop:0 %5, stop:1 %6); }")
                .arg(top.name(), bottom.name(), light.name(), dark.name(),
                     mixed(bm.color, 0.58).name(), mixed(bm.color, 0.44).name()));

        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        bm.button = btn;
    }

    // Connect all buttons with correct settings indices
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        auto& bm = m_bookmarks[i];
        auto* btn = bm.button;
        int sIdx = bm.settingsIndex;

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            if (i < m_bookmarks.size()) {
                emit recallRequested(m_bookmarks[i].entry);
            }
        });

        connect(btn, &QPushButton::customContextMenuRequested, this,
                [this, btn, sIdx]() {
            QMenu menu;
            menu.setStyleSheet(kMenuStyle);
            auto* removeAction = menu.addAction("Remove");
            if (menu.exec(btn->mapToGlobal(QPoint(btn->width(), 0)))
                    == removeAction) {
                emit removeRequested(sIdx);
            }
        });
    }

    if (m_grouped) {
        // Group bookmarks by band (kBands order, "Other" last)
        QMap<int, QVector<int>> bandGroups;  // band index -> bookmark indices
        QVector<int> otherIndices;

        for (int i = 0; i < m_bookmarks.size(); ++i) {
            double freq = m_bookmarks[i].entry.frequencyMhz;
            int bandIdx = -1;
            for (int b = 0; b < kBandCount; ++b) {
                if (freq >= kBands[b].lowMhz && freq <= kBands[b].highMhz) {
                    bandIdx = b;
                    break;
                }
            }
            if (bandIdx >= 0)
                bandGroups[bandIdx].append(i);
            else
                otherIndices.append(i);
        }

        for (int b = 0; b < kBandCount; ++b) {
            if (!bandGroups.contains(b)) continue;
            auto* header = createBandHeader(
                QString(kBands[b].name), kBands[b].lowMhz, kBands[b].highMhz);
            m_buttonLayout->addWidget(header, 0, Qt::AlignHCenter);
            m_bandHeaders.append(header);
            for (int idx : orderedIndices(bandGroups[b])) {
                m_buttonLayout->addWidget(m_bookmarks[idx].button, 0,
                                          Qt::AlignHCenter);
            }
        }

        if (!otherIndices.isEmpty()) {
            auto* header = createBandHeader("Other", 0, 0);
            m_buttonLayout->addWidget(header, 0, Qt::AlignHCenter);
            m_bandHeaders.append(header);
            for (int idx : orderedIndices(otherIndices)) {
                m_buttonLayout->addWidget(m_bookmarks[idx].button, 0,
                                          Qt::AlignHCenter);
            }
        }
    } else {
        // Build 645: by frequency, or in the order the bookmarks were added.
        QVector<int> all;
        for (int i = 0; i < m_bookmarks.size(); ++i)
            all.append(i);
        for (int idx : orderedIndices(all))
            m_buttonLayout->addWidget(m_bookmarks[idx].button, 0, Qt::AlignHCenter);
    }

    m_buttonLayout->addStretch(1);
}

QVector<int> BandStackPanel::orderedIndices(QVector<int> indices) const
{
    // Build 645: lowest frequency on top with "Group by freq"; otherwise the
    // order of adding (createdAtMs; legacy entries without a date are the
    // oldest -- first, in their stored order; the store appends anyway).
    if (m_groupByFreq) {
        std::stable_sort(indices.begin(), indices.end(), [this](int a, int b) {
            return m_bookmarks[a].entry.frequencyMhz < m_bookmarks[b].entry.frequencyMhz;
        });
    } else {
        std::stable_sort(indices.begin(), indices.end(), [this](int a, int b) {
            return std::max<qint64>(0, m_bookmarks[a].entry.createdAtMs)
                 < std::max<qint64>(0, m_bookmarks[b].entry.createdAtMs);
        });
    }
    return indices;
}

void BandStackPanel::setGrouped(bool grouped)
{
    if (m_grouped == grouped) return;
    m_grouped = grouped;
    rebuildLayout();
}

void BandStackPanel::setAutoExpiryMinutes(int minutes)
{
    m_autoExpiryMinutes = minutes;
}

void BandStackPanel::setAutoSaveDwellSeconds(int seconds)
{
    m_autoSaveDwellSeconds = seconds;
}

void BandStackPanel::clear()
{
    qDeleteAll(m_bandHeaders);
    m_bandHeaders.clear();
    for (auto& bm : m_bookmarks) {
        m_buttonLayout->removeWidget(bm.button);
        delete bm.button;
    }
    m_bookmarks.clear();
}

} // namespace AetherSDR
