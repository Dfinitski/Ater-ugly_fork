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

    // ── VFO flag, step 2: slice side buttons, FM, RIT / XIT, autotune (build 774)
    {"vfo.slice.close", "VfoWidget.cpp: slice close button (sliceBtnStyle + ...)",
     /*original*/
     "QPushButton { background: rgba(255,255,255,30); border: none; "
     "border-radius: 10px; font-size: 11px; padding: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,60); }"
     "QPushButton { color: #c8d8e8; }"
     "QPushButton:hover { background: rgba(204,32,32,180); color: #ffffff; }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,30); border: none; "
     "border-radius: 10px; font-size: 11px; padding: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,60); }"
     "QPushButton { color: {{color.button.vfo.slice.text}}; }"
     "QPushButton:hover { background: rgba(204,32,32,180); color: {{color.button.vfo.slice.close.hover.text}}; }"},

    {"vfo.slice.lock", "VfoWidget.cpp: slice lock button (sliceBtnStyle + ..., translucent colours only)",
     /*original*/
     "QPushButton { background: rgba(255,255,255,30); border: none; "
     "border-radius: 10px; font-size: 11px; padding: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,60); }"
     "QPushButton:checked { background: rgba(255,100,100,80); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,30); border: none; "
     "border-radius: 10px; font-size: 11px; padding: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,60); }"
     "QPushButton:checked { background: rgba(255,100,100,80); }"},

    {"vfo.slice.play", "VfoWidget.cpp: slice play button (sliceBtnStyle + ...)",
     /*original*/
     "QPushButton { background: rgba(255,255,255,30); border: none; "
     "border-radius: 10px; font-size: 11px; padding: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,60); }"
     "QPushButton { color: #60a070; }"
     "QPushButton:checked { color: #30d050; background: rgba(50,200,80,110); }"
     "QPushButton:disabled { color: #484848; background: rgba(255,255,255,15); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,30); border: none; "
     "border-radius: 10px; font-size: 11px; padding: 0; }"
     "QPushButton:hover { background: rgba(255,255,255,60); }"
     "QPushButton { color: {{color.button.vfo.slice.play.text}}; }"
     "QPushButton:checked { color: {{color.button.vfo.slice.play.checked.text}}; background: rgba(50,200,80,110); }"
     "QPushButton:disabled { color: {{color.button.vfo.slice.play.disabled.text}}; background: rgba(255,255,255,15); }"},

    {"vfo.offset", "VfoWidget.cpp: FM offset -, Simplex, + (kDirBtn + :disabled)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px;"
     " color: #c8d8e8; font-size: 11px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #0090e0; }"
     "QPushButton:hover { border: 1px solid #0090e0; }"
     "QPushButton:disabled { color: {{color.text.disabled}}; "
     "background: {{color.background.2}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.background}}; border: 1px solid {{color.button.vfo.border}}; border-radius: 2px;"
     " color: {{color.button.vfo.text}}; font-size: 11px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background: {{color.button.vfo.selected.background}}; color: {{color.button.vfo.selected.text}}; border: 1px solid {{color.button.vfo.selected.border}}; }"
     "QPushButton:hover { border: 1px solid {{color.button.vfo.hover.border}}; }"
     "QPushButton:disabled { color: {{color.button.vfo.gated.text}}; "
     "background: {{color.button.vfo.gated.background}}; }"},

    {"vfo.rev", "VfoWidget.cpp: FM REV (kRevBtn + :disabled)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px;"
     " color: #c8d8e8; font-size: 11px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background-color: #604000; color: #ffb800; border: 1px solid #906000; }"
     "QPushButton:hover { border: 1px solid #0090e0; }"
     "QPushButton:disabled { color: {{color.text.disabled}}; "
     "background: {{color.background.2}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.background}}; border: 1px solid {{color.button.vfo.border}}; border-radius: 2px;"
     " color: {{color.button.vfo.text}}; font-size: 11px; font-weight: bold; padding: 2px 4px; }"
     "QPushButton:checked { background-color: {{color.button.vfo.offset.checked.background}}; color: {{color.button.vfo.offset.checked.text}}; border: 1px solid {{color.button.vfo.offset.checked.border}}; }"
     "QPushButton:hover { border: 1px solid {{color.button.vfo.hover.border}}; }"
     "QPushButton:disabled { color: {{color.button.vfo.gated.text}}; "
     "background: {{color.button.vfo.gated.background}}; }"},

    {"vfo.rit", "VfoWidget.cpp: RIT, XIT on (kRitBtnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
     "border-radius: 2px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
     "padding: 1px 4px; }"
     "QPushButton:checked { background-color: #604000; color: #ffb800; "
     "border: 1px solid #906000; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.background}}; border: 1px solid {{color.button.vfo.border}}; "
     "border-radius: 2px; color: {{color.button.vfo.text}}; font-size: 11px; font-weight: bold; "
     "padding: 1px 4px; }"
     "QPushButton:checked { background-color: {{color.button.vfo.offset.checked.background}}; color: {{color.button.vfo.offset.checked.text}}; "
     "border: 1px solid {{color.button.vfo.offset.checked.border}}; }"},

    {"vfo.zero", "VfoWidget.cpp: RIT / XIT zero (kZeroBtnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
     "border-radius: 2px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
     "padding: 1px 4px; }"
     "QPushButton:hover { background: #203040; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.background}}; border: 1px solid {{color.button.vfo.border}}; "
     "border-radius: 2px; color: {{color.button.vfo.text}}; font-size: 11px; font-weight: bold; "
     "padding: 1px 4px; }"
     "QPushButton:hover { background: {{color.button.vfo.hover.background}}; }"},

    {"vfo.autotune", "VfoWidget.cpp: CW autotune Once / Loop, Zero Beat (btnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050; "
     "border-radius: 3px; font-size: 10px; padding: 0 8px; }"
     "QPushButton:hover { background: #253545; }"
     "QPushButton:pressed { background: #00607a; }"
     "QPushButton:checked { background: #00607a; color: #e0f0ff; border-color: #00b4d8; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.background}}; color: {{color.button.vfo.text}}; border: 1px solid {{color.button.vfo.border}}; "
     "border-radius: 3px; font-size: 10px; padding: 0 8px; }"
     "QPushButton:hover { background: {{color.button.vfo.autotune.hover.background}}; }"
     "QPushButton:pressed { background: {{color.button.vfo.autotune.pressed.background}}; }"
     "QPushButton:checked { background: {{color.button.vfo.autotune.checked.background}}; color: {{color.button.vfo.autotune.checked.text}}; border-color: {{color.button.vfo.autotune.checked.border}}; }"},

    // ── VFO flag, step 3: record, mute, TX / split badges (build 775) ───────
    {"vfo.record", "VfoWidget.cpp: slice record button at start (sliceBtnStyle + ...)",
     /*original*/
     "QPushButton { background: rgba(255,255,255,30); border: none; border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover { background: rgba(255,255,255,60); "
     "}QPushButton { color: #c06060; }QPushButton:checked { color: #ff2020; background: "
     "rgba(255,50,50,110); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,30); border: none; border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover { background: rgba(255,255,255,60); "
     "}QPushButton { color: {{color.button.vfo.record.text}}; }QPushButton:checked { color: "
     "{{color.button.vfo.record.checked.text}}; background: rgba(255,50,50,110); }"},

    {"vfo.record.idle", "VfoWidget.cpp: setRecordOn(false), the record button after recording",
     /*original*/
     "QPushButton { background: rgba(255,255,255,30); border: none; border-radius: 10px; "
     "font-size: 11px; padding: 0; color: #c06060; }QPushButton:checked { color: #ff2020; "
     "background: rgba(255,50,50,110); }QPushButton:hover { background: rgba(255,255,255,60); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,30); border: none; border-radius: 10px; "
     "font-size: 11px; padding: 0; color: {{color.button.vfo.record.text}}; }QPushButton:checked "
     "{ color: {{color.button.vfo.record.checked.text}}; background: rgba(255,50,50,110); "
     "}QPushButton:hover { background: rgba(255,255,255,60); }"},

    {"vfo.record.pulse.bright", "VfoWidget.cpp: record pulse, bright half",
     /*original*/
     "QPushButton { background: rgba(255,255,255,30); border: none; border-radius: 10px; "
     "font-size: 11px; padding: 0; color: #ff3030; background: rgba(255,50,50,120); "
     "}QPushButton:hover { background: rgba(255,255,255,60); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,30); border: none; border-radius: 10px; "
     "font-size: 11px; padding: 0; color: {{color.button.vfo.record.pulse.text}}; background: "
     "rgba(255,50,50,120); }QPushButton:hover { background: rgba(255,255,255,60); }"},

    {"vfo.record.pulse.dim", "VfoWidget.cpp: record pulse, dim half",
     /*original*/
     "QPushButton { background: rgba(255,255,255,30); border: none; border-radius: 10px; "
     "font-size: 11px; padding: 0; color: #a03030; background: rgba(255,50,50,50); "
     "}QPushButton:hover { background: rgba(255,255,255,60); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,30); border: none; border-radius: 10px; "
     "font-size: 11px; padding: 0; color: {{color.button.vfo.record.pulse.dim.text}}; "
     "background: rgba(255,50,50,50); }QPushButton:hover { background: rgba(255,255,255,60); }"},

    {"vfo.mute", "VfoWidget.cpp: audio tab MUTE",
     /*original*/
     "QPushButton { background: {{color.background.1}}; border: 1px solid "
     "{{color.background.2}}; border-radius: 2px; color: {{color.text.primary}}; font-size: "
     "13px; font-weight: bold; padding: 1px 4px; }QPushButton:checked { background: #6a2020; "
     "color: {{color.accent.danger}}; border: 1px solid #a04040; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.mute.background}}; border: 1px solid "
     "{{color.button.vfo.mute.border}}; border-radius: 2px; color: "
     "{{color.button.vfo.mute.text}}; font-size: 13px; font-weight: bold; padding: 1px 4px; "
     "}QPushButton:checked { background: {{color.button.vfo.mute.checked.background}}; color: "
     "{{color.button.vfo.mute.checked.text}}; border: 1px solid "
     "{{color.button.vfo.mute.checked.border}}; }"},

    {"vfo.badge.tx", "VfoWidget.cpp: TX badge, transmit slice (updateTxBadgeStyle)",
     /*original*/
     "QPushButton { background: #cc0000; color: {{color.text.primary}}; border: none; "
     "border-radius: 2px; font-size: 12px; font-weight: bold; padding: 0; }QPushButton:hover { "
     "background: #ff2222; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.badge.tx.background}}; color: "
     "{{color.button.vfo.badge.tx.text}}; border: none; border-radius: 2px; font-size: 12px; "
     "font-weight: bold; padding: 0; }QPushButton:hover { background: "
     "{{color.button.vfo.badge.tx.hover.background}}; }"},

    {"vfo.badge.rx", "VfoWidget.cpp: TX badge, not the transmit slice (updateTxBadgeStyle)",
     /*original*/
     "QPushButton { background: #404040; color: {{color.text.label}}; border: none; "
     "border-radius: 2px; font-size: 12px; font-weight: bold; padding: 0; }QPushButton:hover { "
     "background: #606060; color: #c0c0c0; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.badge.rx.background}}; color: "
     "{{color.button.vfo.badge.rx.text}}; border: none; border-radius: 2px; font-size: 12px; "
     "font-weight: bold; padding: 0; }QPushButton:hover { background: "
     "{{color.button.vfo.badge.rx.hover.background}}; color: "
     "{{color.button.vfo.badge.rx.hover.text}}; }"},

    {"vfo.split.swap", "VfoWidget.cpp: SWAP badge (updateSplitBadge)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; color: #80c0ff; border: none; "
     "border-radius: 2px; font-size: 11px; font-weight: bold; padding: 0px 3px; "
     "}QPushButton:hover { background: #306080; color: {{color.text.primary}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.split.swap.background}}; color: "
     "{{color.button.vfo.split.swap.text}}; border: none; border-radius: 2px; font-size: 11px; "
     "font-weight: bold; padding: 0px 3px; }QPushButton:hover { background: "
     "{{color.button.vfo.split.swap.hover.background}}; color: "
     "{{color.button.vfo.split.swap.hover.text}}; }"},

    {"vfo.split.on", "VfoWidget.cpp: SPLIT badge, split on (updateSplitBadge)",
     /*original*/
     "QPushButton { background: #cc0000; color: {{color.text.primary}}; border: none; "
     "border-radius: 2px; font-size: 11px; font-weight: bold; padding: 0px 3px; "
     "}QPushButton:hover { background: #ee2222; }",
     /*themed*/
     "QPushButton { background: {{color.button.vfo.split.on.background}}; color: "
     "{{color.button.vfo.split.on.text}}; border: none; border-radius: 2px; font-size: 11px; "
     "font-weight: bold; padding: 0px 3px; }QPushButton:hover { background: "
     "{{color.button.vfo.split.on.hover.background}}; }"},

    {"vfo.split.off", "VfoWidget.cpp: SPLIT label ghosted (constructor, updateSplitBadge, setSplitTextLit(false); translucent colours only)",
     /*original*/
     "QPushButton { background: transparent; border: none; color: rgba(255,255,255,120); "
     "font-size: 11px; font-weight: bold; padding: 0px 3px; }QPushButton:hover { color: "
     "rgba(255,255,255,180); }",
     /*themed*/
     "QPushButton { background: transparent; border: none; color: rgba(255,255,255,120); "
     "font-size: 11px; font-weight: bold; padding: 0px 3px; }QPushButton:hover { color: "
     "rgba(255,255,255,180); }"},

    {"vfo.split.lit", "VfoWidget.cpp: SPLIT label lit (setSplitTextLit(true); translucent colours only)",
     /*original*/
     "QPushButton { background: transparent; border: none; color: rgba(255,255,255,235); "
     "font-size: 11px; font-weight: bold; padding: 0px 3px; }QPushButton:hover { color: "
     "rgba(255,255,255,255); }",
     /*themed*/
     "QPushButton { background: transparent; border: none; color: rgba(255,255,255,235); "
     "font-size: 11px; font-weight: bold; padding: 0px 3px; }QPushButton:hover { color: "
     "rgba(255,255,255,255); }"},

    // ── Spectrum overlay menu, step 1: menu column, band and transverter buttons (build 776)
    {"overlay.menu", "SpectrumOverlayMenu.cpp: menu column buttons at rest (kMenuBtnNormal, makeMenuBtn)",
     /*original*/
     "QPushButton { background: rgba(20, 30, 45, 240); border: 1px solid rgba(255, 255, 255, "
     "40); border-radius: 2px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
     "}QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; }",
     /*themed*/
     "QPushButton { background: rgba(20, 30, 45, 240); border: 1px solid rgba(255, 255, 255, "
     "40); border-radius: 2px; color: {{color.button.overlay.text}}; font-size: 11px; "
     "font-weight: bold; }QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px "
     "solid {{color.button.overlay.hover.border}}; }"},

    {"overlay.menu.active", "SpectrumOverlayMenu.cpp: the menu button whose panel is open (kMenuBtnActive)",
     /*original*/
     "QPushButton { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; "
     "border-radius: 2px; color: #ffffff; font-size: 11px; font-weight: bold; }",
     /*themed*/
     "QPushButton { background: rgba(0, 112, 192, 180); border: 1px solid "
     "{{color.button.overlay.active.border}}; border-radius: 2px; color: "
     "{{color.button.overlay.active.text}}; font-size: 11px; font-weight: bold; }"},

    {"overlay.toggle", "SpectrumOverlayMenu.cpp: the arrow that opens the menu",
     /*original*/
     "QPushButton { background: rgba(20, 30, 45, 240); border: 1px solid rgba(255, 255, 255, "
     "40); border-radius: 2px; color: {{color.text.primary}}; font-size: 13px; font-weight: "
     "bold; padding: 0px; }QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px "
     "solid {{color.accent.dim}}; }",
     /*themed*/
     "QPushButton { background: rgba(20, 30, 45, 240); border: 1px solid rgba(255, 255, 255, "
     "40); border-radius: 2px; color: {{color.button.overlay.toggle.text}}; font-size: 13px; "
     "font-weight: bold; padding: 0px; }QPushButton:hover { background: rgba(0, 112, 192, "
     "180); border: 1px solid {{color.button.overlay.toggle.hover.border}}; }"},

    {"overlay.band", "SpectrumOverlayMenu.cpp: band panel buttons (kBandBtnStyle)",
     /*original*/
     "QPushButton { background: rgba(30, 40, 55, 220); border: 1px solid #304050; "
     "border-radius: 3px; padding: 0px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
     "}QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; "
     "}QPushButton:checked { background: {{color.background.2}}; color: "
     "{{color.text.primary}}; border: 2px solid {{color.text.primary}}; }",
     /*themed*/
     "QPushButton { background: rgba(30, 40, 55, 220); border: 1px solid "
     "{{color.button.overlay.border}}; border-radius: 3px; padding: 0px; color: "
     "{{color.button.overlay.text}}; font-size: 11px; font-weight: bold; }QPushButton:hover { "
     "background: rgba(0, 112, 192, 180); border: 1px solid "
     "{{color.button.overlay.hover.border}}; }QPushButton:checked { background: "
     "{{color.button.overlay.checked.background}}; color: "
     "{{color.button.overlay.checked.text}}; border: 2px solid "
     "{{color.button.overlay.checked.border}}; }"},

    {"overlay.xvtr", "SpectrumOverlayMenu.cpp: transverter buttons in the band panel (kXvtrBtnStyle; xvtrNameStyle adds padding / font)",
     /*original*/
     "QPushButton { background: rgba(30, 40, 55, 220); border: 1px solid #304050; "
     "border-radius: 3px; padding: 0px; color: #00d0ff; font-size: 11px; font-weight: bold; "
     "}QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; "
     "}QPushButton:checked { background: {{color.background.2}}; color: "
     "{{color.text.primary}}; border: 2px solid {{color.text.primary}}; }",
     /*themed*/
     "QPushButton { background: rgba(30, 40, 55, 220); border: 1px solid "
     "{{color.button.overlay.border}}; border-radius: 3px; padding: 0px; color: "
     "{{color.button.overlay.xvtr.text}}; font-size: 11px; font-weight: bold; "
     "}QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid "
     "{{color.button.overlay.hover.border}}; }QPushButton:checked { background: "
     "{{color.button.overlay.checked.background}}; color: "
     "{{color.button.overlay.checked.text}}; border: 2px solid "
     "{{color.button.overlay.checked.border}}; }"},

    {"overlay.xvtr.panel", "SpectrumOverlayMenu.cpp: transverter panel buttons, HF (btnStyle; xvtrNameStyle adds padding / font)",
     /*original*/
     "QPushButton { background: rgba(30, 40, 55, 220); border: 1px solid #304050; "
     "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
     "}QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; }",
     /*themed*/
     "QPushButton { background: rgba(30, 40, 55, 220); border: 1px solid "
     "{{color.button.overlay.border}}; border-radius: 3px; color: "
     "{{color.button.overlay.text}}; font-size: 11px; font-weight: bold; }QPushButton:hover { "
     "background: rgba(0, 112, 192, 180); border: 1px solid "
     "{{color.button.overlay.hover.border}}; }"},

    {"overlay.xvtr.blank", "SpectrumOverlayMenu.cpp: empty transverter panel slots (disabledStyle)",
     /*original*/
     "QPushButton { background: rgba(30, 40, 55, 220); border: 1px solid #304050; "
     "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
     "}QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; "
     "}QPushButton:disabled { background: rgba(15, 15, 26, 180); color: #252535; border: 1px "
     "solid #1a1a2a; }",
     /*themed*/
     "QPushButton { background: rgba(30, 40, 55, 220); border: 1px solid "
     "{{color.button.overlay.border}}; border-radius: 3px; color: "
     "{{color.button.overlay.text}}; font-size: 11px; font-weight: bold; }QPushButton:hover { "
     "background: rgba(0, 112, 192, 180); border: 1px solid "
     "{{color.button.overlay.hover.border}}; }QPushButton:disabled { background: rgba(15, 15, "
     "26, 180); color: {{color.button.overlay.disabled.text}}; border: 1px solid "
     "{{color.button.overlay.disabled.border}}; }"},
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
