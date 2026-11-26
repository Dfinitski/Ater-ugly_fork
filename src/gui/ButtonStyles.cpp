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

    // ── RX applet (build 772) ───────────────────────────────────────────────
    {"rx.toggle", "RxApplet.cpp: mkToggle() default (kButtonBase), Filter unavailable",
     /*original*/
     "QPushButton { background: {{color.background.1}}; "
     "border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.background.2}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.background}}; "
     "border: 1px solid {{color.button.rx.border}}; "
     "border-radius: 3px; color: {{color.button.rx.text}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.hover.background}}; }"},

    {"rx.select", "RxApplet.cpp: NB / NR, BIN, AGC A (kButtonBase + kBlueActive)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; "
     "border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.background.2}}; }"
     "QPushButton:checked { background: {{color.accent.dim}}; "
     "color: {{color.text.primary}}; "
     "border: 1px solid {{color.accent.bright}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.background}}; "
     "border: 1px solid {{color.button.rx.border}}; "
     "border-radius: 3px; color: {{color.button.rx.text}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.hover.background}}; }"
     "QPushButton:checked { background: {{color.button.rx.selected.background}}; "
     "color: {{color.button.rx.selected.text}}; "
     "border: 1px solid {{color.button.rx.selected.border}}; }"},

    {"rx.offset", "RxApplet.cpp: FM offset -, Simplex, + (kButtonBase + kBlueActive + :disabled)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; "
     "border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.background.2}}; }"
     "QPushButton:checked { background: {{color.accent.dim}}; "
     "color: {{color.text.primary}}; "
     "border: 1px solid {{color.accent.bright}}; }"
     "QPushButton:disabled { color: {{color.text.disabled}}; "
     "background: {{color.background.2}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.background}}; "
     "border: 1px solid {{color.button.rx.border}}; "
     "border-radius: 3px; color: {{color.button.rx.text}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.hover.background}}; }"
     "QPushButton:checked { background: {{color.button.rx.selected.background}}; "
     "color: {{color.button.rx.selected.text}}; "
     "border: 1px solid {{color.button.rx.selected.border}}; }"
     "QPushButton:disabled { color: {{color.button.rx.gated.text}}; "
     "background: {{color.button.rx.gated.background}}; }"},

    {"rx.rev", "RxApplet.cpp: FM REV (kButtonBase + kAmberActive + :disabled)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; "
     "border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.background.2}}; }"
     "QPushButton:checked { background-color: #604000; color: #ffb800; "
     "border: 1px solid #906000; }"
     "QPushButton:disabled { color: {{color.text.disabled}}; "
     "background: {{color.background.2}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.background}}; "
     "border: 1px solid {{color.button.rx.border}}; "
     "border-radius: 3px; color: {{color.button.rx.text}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.hover.background}}; }"
     "QPushButton:checked { background-color: {{color.button.rx.checked.background}}; color: {{color.button.rx.checked.text}}; "
     "border: 1px solid {{color.button.rx.checked.border}}; }"
     "QPushButton:disabled { color: {{color.button.rx.gated.text}}; "
     "background: {{color.button.rx.gated.background}}; }"},

    {"rx.amber", "RxApplet.cpp: RIT, XIT on (kButtonBase + kAmberActive)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; "
     "border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.background.2}}; }"
     "QPushButton:checked { background-color: #604000; color: #ffb800; "
     "border: 1px solid #906000; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.background}}; "
     "border: 1px solid {{color.button.rx.border}}; "
     "border-radius: 3px; color: {{color.button.rx.text}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.hover.background}}; }"
     "QPushButton:checked { background-color: {{color.button.rx.checked.background}}; color: {{color.button.rx.checked.text}}; "
     "border: 1px solid {{color.button.rx.checked.border}}; }"},

    {"rx.zero", "RxApplet.cpp: RIT / XIT zero (kButtonBase + padding)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; "
     "border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.background.2}}; }"
     "QPushButton { padding: 1px 4px; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.background}}; "
     "border: 1px solid {{color.button.rx.border}}; "
     "border-radius: 3px; color: {{color.button.rx.text}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.hover.background}}; }"
     "QPushButton { padding: 1px 4px; }"},

    {"rx.caption", "RxApplet.cpp: NF + caption, SQL off (kButtonBase + kDisabledBtn)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; "
     "border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.background.2}}; }"
     "QPushButton:disabled { background-color: #1a1a2a; color: #556070; "
     "border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.background}}; "
     "border: 1px solid {{color.button.rx.border}}; "
     "border-radius: 3px; color: {{color.button.rx.text}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.hover.background}}; }"
     "QPushButton:disabled { background-color: {{color.button.rx.disabled.background}}; color: {{color.button.rx.disabled.text}}; "
     "border: 1px solid {{color.button.rx.disabled.border}}; }"},

    {"rx.sql.manual", "RxApplet.cpp: SQL manual (updateSqlButton)",
     /*original*/
     "QPushButton { background: #006040; color: #00ff88; "
     "border: 1px solid #00a060; border-radius: 3px; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: #007050; }"
     "QPushButton:disabled { background-color: #1a1a2a; color: #556070; "
     "border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.sql.manual.background}}; color: {{color.button.rx.sql.manual.text}}; "
     "border: 1px solid {{color.button.rx.sql.manual.border}}; border-radius: 3px; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.sql.manual.hover.background}}; }"
     "QPushButton:disabled { background-color: {{color.button.rx.disabled.background}}; color: {{color.button.rx.disabled.text}}; "
     "border: 1px solid {{color.button.rx.disabled.border}}; }"},

    {"rx.sql.auto", "RxApplet.cpp: SQL AUTO (updateSqlButton)",
     /*original*/
     "QPushButton { background: #604000; color: #ffb800; "
     "border: 1px solid #906000; border-radius: 3px; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: #705000; }"
     "QPushButton:disabled { background-color: #1a1a2a; color: #556070; "
     "border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.sql.auto.background}}; color: {{color.button.rx.sql.auto.text}}; "
     "border: 1px solid {{color.button.rx.sql.auto.border}}; border-radius: 3px; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.sql.auto.hover.background}}; }"
     "QPushButton:disabled { background-color: {{color.button.rx.disabled.background}}; color: {{color.button.rx.disabled.text}}; "
     "border: 1px solid {{color.button.rx.disabled.border}}; }"},

    {"rx.preset", "RxApplet.cpp: filter preset buttons (kButtonBase + kBlueActive + padding)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; "
     "border: 1px solid {{color.background.2}}; "
     "border-radius: 3px; color: {{color.text.primary}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.background.2}}; }"
     "QPushButton:checked { background: {{color.accent.dim}}; "
     "color: {{color.text.primary}}; "
     "border: 1px solid {{color.accent.bright}}; }"
     "QPushButton { padding: 1px; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.background}}; "
     "border: 1px solid {{color.button.rx.border}}; "
     "border-radius: 3px; color: {{color.button.rx.text}}; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.rx.hover.background}}; }"
     "QPushButton:checked { background: {{color.button.rx.selected.background}}; "
     "color: {{color.button.rx.selected.text}}; "
     "border: 1px solid {{color.button.rx.selected.border}}; }"
     "QPushButton { padding: 1px; }"},

    {"rx.lock", "RxApplet.cpp: VFO lock",
     /*original*/
     "QPushButton { font-size: 13px; padding: 0; }"
     "QPushButton:checked { color: #4488ff; }",
     /*themed*/
     "QPushButton { font-size: 13px; padding: 0; }"
     "QPushButton:checked { color: {{color.button.rx.lock.checked.text}}; }"},

    {"rx.ant", "RxApplet.cpp: RX antenna",
     /*original*/
     "QPushButton { color: #4488ff; background: transparent; border: none; "
     "font-size: 10px; font-weight: bold; padding: 0 2px; }"
     "QPushButton:hover { color: #66aaff; }",
     /*themed*/
     "QPushButton { color: {{color.button.rx.ant.text}}; background: transparent; border: none; "
     "font-size: 10px; font-weight: bold; padding: 0 2px; }"
     "QPushButton:hover { color: {{color.button.rx.ant.hover.text}}; }"},

    {"rx.txant", "RxApplet.cpp: TX antenna",
     /*original*/
     "QPushButton { color: {{color.accent.danger}}; background: transparent; border: none; "
     "font-size: 10px; font-weight: bold; padding: 0 2px; }"
     "QPushButton:hover { color: #ff6666; }",
     /*themed*/
     "QPushButton { color: {{color.button.rx.txant.text}}; background: transparent; border: none; "
     "font-size: 10px; font-weight: bold; padding: 0 2px; }"
     "QPushButton:hover { color: {{color.button.rx.txant.hover.text}}; }"},

    {"rx.qsk", "RxApplet.cpp: QSK indicator",
     /*original*/
     "QPushButton { color: {{color.text.secondary}}; background: transparent; border: none; "
     "font-size: 10px; font-weight: bold; padding: 0 2px; }"
     "QPushButton:checked { color: #ffb800; }",
     /*themed*/
     "QPushButton { color: {{color.button.rx.qsk.text}}; background: transparent; border: none; "
     "font-size: 10px; font-weight: bold; padding: 0 2px; }"
     "QPushButton:checked { color: {{color.button.rx.qsk.checked.text}}; }"},

    // ── VFO flag, step 1: the file's style constants (build 773) ────────────
    {"vfo.ant.rx", "VfoWidget.cpp: RX antenna (kFlatBtn + blue)",
     /*original*/
     "QPushButton { background: transparent; border: none; "
     "font-size: 13px; font-weight: bold; padding: 0 6px; margin: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,28); border-radius: 3px; }"
     "QPushButton { color: #4488ff; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; "
     "font-size: 13px; font-weight: bold; padding: 0 6px; margin: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,28); border-radius: 3px; }"
     "QPushButton { color: {{color.button.vfo.ant.rx.text}}; }"},

    {"vfo.ant.tx", "VfoWidget.cpp: TX antenna (kFlatBtn + red)",
     /*original*/
     "QPushButton { background: transparent; border: none; "
     "font-size: 13px; font-weight: bold; padding: 0 6px; margin: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,28); border-radius: 3px; }"
     "QPushButton { color: #ff4444; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; "
     "font-size: 13px; font-weight: bold; padding: 0 6px; margin: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,28); border-radius: 3px; }"
     "QPushButton { color: {{color.button.vfo.ant.tx.text}}; }"},

    {"vfo.tab", "VfoWidget.cpp: tab labels at rest (kTabLblNormal)",
     /*original*/
     "QPushButton { background: transparent; border: none; "
     "border-bottom: 2px solid transparent; "
     "color: #a8b5c3; font-size: 13px; font-weight: bold; padding: 3px 0; }"
     "QPushButton:hover { color: #ffffff; border-bottom: 2px solid #5a7890; }"
     "QPushButton:focus { outline: none; border-bottom: 2px solid #6888a0; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; "
     "border-bottom: 2px solid transparent; "
     "color: {{color.button.vfo.tab.text}}; font-size: 13px; font-weight: bold; padding: 3px 0; }"
     "QPushButton:hover { color: {{color.button.vfo.tab.hover.text}}; border-bottom: 2px solid {{color.button.vfo.tab.hover.border}}; }"
     "QPushButton:focus { outline: none; border-bottom: 2px solid {{color.button.vfo.tab.focus.border}}; }"},

    {"vfo.tab.active", "VfoWidget.cpp: the open tab (kTabLblActive)",
     /*original*/
     "QPushButton { background: transparent; border: none; "
     "border-bottom: 2px solid #00b4d8; "
     "color: #00b4d8; font-size: 13px; font-weight: bold; padding: 3px 0; }"
     "QPushButton:focus { outline: none; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; "
     "border-bottom: 2px solid {{color.button.vfo.tab.active.border}}; "
     "color: {{color.button.vfo.tab.active.text}}; font-size: 13px; font-weight: bold; padding: 3px 0; }"
     "QPushButton:focus { outline: none; }"},

    {"vfo.tab.dsp", "VfoWidget.cpp: DSP tab closed with DSP on (kTabLblDspActive)",
     /*original*/
     "QPushButton { background: transparent; border: none; "
     "border-bottom: 2px solid #20a040; "
     "color: #20a040; font-size: 13px; font-weight: bold; padding: 3px 0; }"
     "QPushButton:focus { outline: none; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; "
     "border-bottom: 2px solid {{color.button.vfo.tab.dsp.border}}; "
     "color: {{color.button.vfo.tab.dsp.text}}; font-size: 13px; font-weight: bold; padding: 3px 0; }"
     "QPushButton:focus { outline: none; }"},

    {"vfo.dsp", "VfoWidget.cpp: DSP toggles, DIV, ESC, ESC +180, AetherDSP, AetherVoice (kDspToggle)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; "
     "color: #c8d8e8; font-size: 13px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background: #1a6030; color: #ffffff; border: 1px solid #20a040; }"
     "QPushButton:hover { border: 1px solid #0090e0; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.background}}; border: 1px solid {{color.button.vfo.border}}; border-radius: 2px; "
     "color: {{color.button.vfo.text}}; font-size: 13px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background: {{color.button.vfo.checked.background}}; color: {{color.button.vfo.checked.text}}; border: 1px solid {{color.button.vfo.checked.border}}; }"
     "QPushButton:hover { border: 1px solid {{color.button.vfo.hover.border}}; }"},

    {"vfo.dsp.caption", "VfoWidget.cpp: SQL off, NF caption (kDspToggle + kDisabledBtn)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; "
     "color: #c8d8e8; font-size: 13px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background: #1a6030; color: #ffffff; border: 1px solid #20a040; }"
     "QPushButton:hover { border: 1px solid #0090e0; }"
     "QPushButton:disabled { background-color: #1a1a2a; color: #556070; "
     "border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.background}}; border: 1px solid {{color.button.vfo.border}}; border-radius: 2px; "
     "color: {{color.button.vfo.text}}; font-size: 13px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background: {{color.button.vfo.checked.background}}; color: {{color.button.vfo.checked.text}}; border: 1px solid {{color.button.vfo.checked.border}}; }"
     "QPushButton:hover { border: 1px solid {{color.button.vfo.hover.border}}; }"
     "QPushButton:disabled { background-color: {{color.button.vfo.disabled.background}}; color: {{color.button.vfo.disabled.text}}; "
     "border: 1px solid {{color.button.vfo.disabled.border}}; }"},

    {"vfo.dsp.active", "VfoWidget.cpp: AetherDSP launcher with a client NR on (kDspToggleActive)",
     /*original*/
     "QPushButton { background: #1a6030; border: 1px solid #20a040; border-radius: 2px; "
     "color: #ffffff; font-size: 13px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:hover { border: 1px solid #0090e0; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.checked.background}}; border: 1px solid {{color.button.vfo.checked.border}}; border-radius: 2px; "
     "color: {{color.button.vfo.checked.text}}; font-size: 13px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:hover { border: 1px solid {{color.button.vfo.hover.border}}; }"},

    {"vfo.mode", "VfoWidget.cpp: quick mode buttons, filter presets (kModeBtn)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; "
     "color: #c8d8e8; font-size: 13px; font-weight: bold; padding: 3px; }"
     "QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #0090e0; }"
     "QPushButton:hover { border: 1px solid #0090e0; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.background}}; border: 1px solid {{color.button.vfo.border}}; border-radius: 2px; "
     "color: {{color.button.vfo.text}}; font-size: 13px; font-weight: bold; padding: 3px; }"
     "QPushButton:checked { background: {{color.button.vfo.selected.background}}; color: {{color.button.vfo.selected.text}}; border: 1px solid {{color.button.vfo.selected.border}}; }"
     "QPushButton:hover { border: 1px solid {{color.button.vfo.hover.border}}; }"},

    {"vfo.meter", "VfoWidget.cpp: meter view selector (kMeterOptBtn)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; "
     "color: #c8d8e8; font-size: 13px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #0090e0; }"
     "QPushButton:hover { border: 1px solid #0090e0; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.background}}; border: 1px solid {{color.button.vfo.border}}; border-radius: 2px; "
     "color: {{color.button.vfo.text}}; font-size: 13px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background: {{color.button.vfo.selected.background}}; color: {{color.button.vfo.selected.text}}; border: 1px solid {{color.button.vfo.selected.border}}; }"
     "QPushButton:hover { border: 1px solid {{color.button.vfo.hover.border}}; }"},

    {"vfo.sql.manual", "VfoWidget.cpp: SQL manual / on",
     /*original*/
     "QPushButton { background: #006040; color: #00ff88; "
     "border: 1px solid #00a060; border-radius: 3px; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: #007050; }"
     "QPushButton:disabled { background-color: #1a1a2a; color: #556070; "
     "border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.sql.manual.background}}; color: {{color.button.vfo.sql.manual.text}}; "
     "border: 1px solid {{color.button.vfo.sql.manual.border}}; border-radius: 3px; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.vfo.sql.manual.hover.background}}; }"
     "QPushButton:disabled { background-color: {{color.button.vfo.disabled.background}}; color: {{color.button.vfo.disabled.text}}; "
     "border: 1px solid {{color.button.vfo.disabled.border}}; }"},

    {"vfo.sql.auto", "VfoWidget.cpp: SQL AUTO",
     /*original*/
     "QPushButton { background: #604000; color: #ffb800; "
     "border: 1px solid #906000; border-radius: 3px; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: #705000; }"
     "QPushButton:disabled { background-color: #1a1a2a; color: #556070; "
     "border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.sql.auto.background}}; color: {{color.button.vfo.sql.auto.text}}; "
     "border: 1px solid {{color.button.vfo.sql.auto.border}}; border-radius: 3px; "
     "font-size: 10px; font-weight: bold; padding: 1px 2px; }"
     "QPushButton:hover { background: {{color.button.vfo.sql.auto.hover.background}}; }"
     "QPushButton:disabled { background-color: {{color.button.vfo.disabled.background}}; color: {{color.button.vfo.disabled.text}}; "
     "border: 1px solid {{color.button.vfo.disabled.border}}; }"},
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
