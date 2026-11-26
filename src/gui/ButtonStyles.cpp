#include "ButtonStyles.h"

#include "core/AppSettings.h"
#include "core/ThemeManager.h"

#include <QString>
#include <QWidget>

#include <cstring>

namespace AetherSDR::ButtonStyles {

namespace {

struct Entry {
    const char* id;
    const char* site;       // where it is used (for a manual restore)
    const char* original;   // the sheet exactly as it stood in the code
    const char* themed;     // the same with its colours as tokens
};

// The entries below are read by tools/check_button_styles.py: keep the form
// {"id", "site", /*original*/ "..." ..., /*themed*/ "..." ...},
// BEGIN-ENTRIES
const Entry kEntries[] = {
    // ── TX applet (build 770) ───────────────────────────────────────────────
    {"tx.action", "TxApplet.cpp: TUNE (idle, and back to idle after a tune), ATU, MEM",
     /*original*/
     "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
     "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; "
     "padding: 2px; }"
     "QPushButton:hover { background: #204060; }"
     "QPushButton:disabled { background-color: #1a1a2a; color: #556070; "
     "border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.tx.background}}; border: 1px solid {{color.button.tx.border}}; "
     "border-radius: 3px; color: {{color.button.tx.text}}; font-size: 10px; font-weight: bold; "
     "padding: 2px; }"
     "QPushButton:hover { background: {{color.button.tx.hover.background}}; }"
     "QPushButton:disabled { background-color: {{color.button.tx.disabled.background}}; color: {{color.button.tx.disabled.text}}; "
     "border: 1px solid {{color.button.tx.disabled.border}}; }"},

    {"tx.toggle", "TxApplet.cpp: MON, DUP",
     /*original*/
     "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
     "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; "
     "padding: 2px; }"
     "QPushButton:hover { background: #204060; }"
     "QPushButton:disabled { background-color: #1a1a2a; color: #556070; "
     "border: 1px solid #2a3040; }"
     "QPushButton:checked { background: #006030; border: 1px solid #008040; color: #ffffff; }",
     /*themed*/
     "QPushButton { background: {{color.button.tx.background}}; border: 1px solid {{color.button.tx.border}}; "
     "border-radius: 3px; color: {{color.button.tx.text}}; font-size: 10px; font-weight: bold; "
     "padding: 2px; }"
     "QPushButton:hover { background: {{color.button.tx.hover.background}}; }"
     "QPushButton:disabled { background-color: {{color.button.tx.disabled.background}}; color: {{color.button.tx.disabled.text}}; "
     "border: 1px solid {{color.button.tx.disabled.border}}; }"
     "QPushButton:checked { background: {{color.button.tx.checked.background}}; border: 1px solid {{color.button.tx.checked.border}}; color: {{color.button.tx.checked.text}}; }"},

    {"tx.mox.idle", "TxApplet.cpp: MOX at rest (kMoxIdleStyle)",
     /*original*/
     "QPushButton { background: #1a3a5a; border: 1px solid {{color.tx.mox.border}}; "
     "border-radius: 3px; color: {{color.tx.mox.text}}; font-size: 10px; font-weight: bold; "
     "padding: 2px; }"
     "QPushButton:hover { background: #204060; border: 1px solid {{color.tx.mox.border.hover}}; "
     "color: {{color.tx.mox.text.hover}}; }"
     "QPushButton:disabled { background-color: #1a1a2a; color: #556070; "
     "border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.tx.background}}; border: 1px solid {{color.tx.mox.border}}; "
     "border-radius: 3px; color: {{color.tx.mox.text}}; font-size: 10px; font-weight: bold; "
     "padding: 2px; }"
     "QPushButton:hover { background: {{color.button.tx.hover.background}}; border: 1px solid {{color.tx.mox.border.hover}}; "
     "color: {{color.tx.mox.text.hover}}; }"
     "QPushButton:disabled { background-color: {{color.button.tx.disabled.background}}; color: {{color.button.tx.disabled.text}}; "
     "border: 1px solid {{color.button.tx.disabled.border}}; }"},

    {"tx.mox.active", "TxApplet.cpp: MOX while transmitting",
     /*original*/
     "QPushButton { background: #cc2222; border: 1px solid #ff4444; "
     "border-radius: 3px; color: #ffffff; font-size: 10px; font-weight: bold; "
     "padding: 2px; }",
     /*themed*/
     "QPushButton { background: {{color.button.tx.active.background}}; border: 1px solid {{color.button.tx.active.border}}; "
     "border-radius: 3px; color: {{color.button.tx.active.text}}; font-size: 10px; font-weight: bold; "
     "padding: 2px; }"},

    {"tx.tune.active", "TxApplet.cpp: TUNE while tuning (TUNING...)",
     /*original*/
     "QPushButton { background: #cc2222; border: 1px solid {{color.accent.danger}}; "
     "border-radius: 3px; color: {{color.text.primary}}; font-size: 10px; font-weight: bold; "
     "padding: 2px; }",
     /*themed*/
     "QPushButton { background: {{color.button.tx.active.background}}; border: 1px solid {{color.accent.danger}}; "
     "border-radius: 3px; color: {{color.text.primary}}; font-size: 10px; font-weight: bold; "
     "padding: 2px; }"},

    {"tx.apd", "TxApplet.cpp: APD",
     /*original*/
     "QPushButton { background: {{color.background.2}}; border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; font-size: 10px; font-weight: bold; }"
     "QPushButton:checked { background: #006030; border: 1px solid #008040; color: {{color.text.primary}}; }"
     "QPushButton:hover { background: {{color.background.1}}; }",
     /*themed*/
     "QPushButton { background: {{color.background.2}}; border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; font-size: 10px; font-weight: bold; }"
     "QPushButton:checked { background: {{color.button.tx.checked.background}}; border: 1px solid {{color.button.tx.checked.border}}; color: {{color.text.primary}}; }"
     "QPushButton:hover { background: {{color.background.1}}; }"},

    // ── Mic applet (build 771) ──────────────────────────────────────────────
    {"mic.toggle", "PhoneCwApplet.cpp: ACC, COMP, MON, DUP; CW: sidetone, break-in, APF (kButtonBase + kGreenActive)",
     /*original*/
     "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
     "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; }"
     "QPushButton[continuousCompressor=\"true\"] { font-size: 11px; font-weight: normal; }"
     "QPushButton:hover { background: #204060; }"
     "QPushButton:checked { background-color: #006040; color: #00ff88; "
     "border: 1px solid #00a060; }",
     /*themed*/
     "QPushButton { background: {{color.button.mic.background}}; border: 1px solid {{color.button.mic.border}}; "
     "border-radius: 3px; color: {{color.button.mic.text}}; font-size: 10px; font-weight: bold; }"
     "QPushButton[continuousCompressor=\"true\"] { font-size: 11px; font-weight: normal; }"
     "QPushButton:hover { background: {{color.button.mic.hover.background}}; }"
     "QPushButton:checked { background-color: {{color.button.mic.checked.background}}; color: {{color.button.mic.checked.text}}; "
     "border: 1px solid {{color.button.mic.checked.border}}; }"},

    {"mic.select", "PhoneCwApplet.cpp: DAX, Profile 1-4; CW: iambic, hold delay (kButtonBase + kBlueActive)",
     /*original*/
     "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
     "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; }"
     "QPushButton[continuousCompressor=\"true\"] { font-size: 11px; font-weight: normal; }"
     "QPushButton:hover { background: #204060; }"
     "QPushButton:checked { background-color: #0070c0; color: #ffffff; "
     "border: 1px solid #0090e0; }"
     "QPushButton[holdUnarmed=\"true\"]:checked { "
     "background-color: rgba(0, 112, 192, 0.35); "
     "color: rgba(255, 255, 255, 0.6); "
     "border: 1px dashed rgba(0, 144, 224, 0.5); }",
     /*themed*/
     "QPushButton { background: {{color.button.mic.background}}; border: 1px solid {{color.button.mic.border}}; "
     "border-radius: 3px; color: {{color.button.mic.text}}; font-size: 10px; font-weight: bold; }"
     "QPushButton[continuousCompressor=\"true\"] { font-size: 11px; font-weight: normal; }"
     "QPushButton:hover { background: {{color.button.mic.hover.background}}; }"
     "QPushButton:checked { background-color: {{color.button.mic.selected.background}}; color: {{color.button.mic.selected.text}}; "
     "border: 1px solid {{color.button.mic.selected.border}}; }"
     "QPushButton[holdUnarmed=\"true\"]:checked { "
     "background-color: rgba(0, 112, 192, 0.35); "
     "color: rgba(255, 255, 255, 0.6); "
     "border: 1px dashed rgba(0, 144, 224, 0.5); }"},
};
// END-ENTRIES

const Entry* find(const char* id)
{
    for (const Entry& e : kEntries) {
        if (std::strcmp(e.id, id) == 0)
            return &e;
    }
    return nullptr;
}

} // namespace

bool themedEnabled()
{
    return AppSettings::instance().value(QStringLiteral("ThemedButtons"), QStringLiteral("True"))
               .toString() != QLatin1String("False");
}

QString sheet(const char* id)
{
    const Entry* e = find(id);
    if (!e)
        return QString();
    return QString::fromLatin1(themedEnabled() ? e->themed : e->original);
}

void apply(QWidget* widget, const char* id)
{
    if (widget)
        ThemeManager::instance().applyStyleSheet(widget, sheet(id));
}

} // namespace AetherSDR::ButtonStyles
