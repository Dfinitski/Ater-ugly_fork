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

    // Build 786: new -- the X/RIT tab closed while RIT or XIT holds an offset
    // (amber, the RIT / XIT buttons' colour).
    {"vfo.tab.rit", "VfoWidget.cpp: X/RIT tab closed with a RIT / XIT offset (updateDspTabAccent)",
     /*original*/
     "QPushButton { background: transparent; border: none; "
     "border-bottom: 2px solid #ffb800; "
     "color: #ffb800; font-size: 13px; font-weight: bold; padding: 3px 0; }"
     "QPushButton:focus { outline: none; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; "
     "border-bottom: 2px solid {{color.button.vfo.tab.rit.border}}; "
     "color: {{color.button.vfo.tab.rit.text}}; font-size: 13px; font-weight: bold; padding: 3px 0; }"
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

    // ── Spectrum overlay menu, step 2: ANT panel, front end, WNB, WFM, Display panel (build 777)
    {"overlay.ant", "SpectrumOverlayMenu.cpp: ANT panel RX / TX antenna buttons, LNA (antBtnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; color: "
     "#c8d8e8; font-size: 10px; font-weight: bold; padding: 0px; }QPushButton:hover { border: "
     "1px solid #0090e0; }QPushButton:checked { background: #0070c0; border: 1px solid "
     "#0090e0; color: #ffffff; }QPushButton:disabled { color: #607080; "
     "}QPushButton:checked:disabled { background: #304a60; color: #a0b0c0; }",
     /*themed*/
     "QPushButton { background: {{color.button.overlay.ant.background}}; border: 1px solid "
     "{{color.button.overlay.ant.border}}; border-radius: 2px; color: "
     "{{color.button.overlay.ant.text}}; font-size: 10px; font-weight: bold; padding: 0px; "
     "}QPushButton:hover { border: 1px solid {{color.button.overlay.ant.hover.border}}; "
     "}QPushButton:checked { background: {{color.button.overlay.ant.checked.background}}; "
     "border: 1px solid {{color.button.overlay.ant.checked.border}}; color: "
     "{{color.button.overlay.ant.checked.text}}; }QPushButton:disabled { color: "
     "{{color.button.overlay.ant.disabled.text}}; }QPushButton:checked:disabled { background: "
     "{{color.button.overlay.ant.checked.disabled.background}}; color: "
     "{{color.button.overlay.ant.checked.disabled.text}}; }"},

    {"overlay.loop", "SpectrumOverlayMenu.cpp: ANT panel LoopA / LoopB (loopBtnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; color: "
     "#c8d8e8; font-size: 10px; font-weight: bold; }QPushButton:checked { background: #0070c0; "
     "color: #ffffff; border: 1px solid #0090e0; }QPushButton:hover { border: 1px solid "
     "#0090e0; }",
     /*themed*/
     "QPushButton { background: {{color.button.overlay.ant.background}}; border: 1px solid "
     "{{color.button.overlay.ant.border}}; border-radius: 2px; color: "
     "{{color.button.overlay.ant.text}}; font-size: 10px; font-weight: bold; "
     "}QPushButton:checked { background: {{color.button.overlay.ant.checked.background}}; "
     "color: {{color.button.overlay.ant.checked.text}}; border: 1px solid "
     "{{color.button.overlay.ant.checked.border}}; }QPushButton:hover { border: 1px solid "
     "{{color.button.overlay.ant.hover.border}}; }"},

    {"overlay.adc", "SpectrumOverlayMenu.cpp: ANT panel ADC button (adcBtnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; color: "
     "#c8d8e8; font-size: 10px; font-weight: bold; padding: 0px; }QPushButton:hover { border: "
     "1px solid #0090e0; }",
     /*themed*/
     "QPushButton { background: {{color.button.overlay.ant.background}}; border: 1px solid "
     "{{color.button.overlay.ant.border}}; border-radius: 2px; color: "
     "{{color.button.overlay.ant.text}}; font-size: 10px; font-weight: bold; padding: 0px; "
     "}QPushButton:hover { border: 1px solid {{color.button.overlay.ant.hover.border}}; }"},

    {"overlay.check", "SpectrumOverlayMenu.cpp: ANT panel dither / random check boxes (chkStyle)",
     /*original*/
     "QCheckBox { spacing: 0px; }QCheckBox::indicator { width: 12px; height: 12px; border: 1px "
     "solid #304050; border-radius: 2px; background: #0a0a18; }QCheckBox::indicator:hover { "
     "border: 1px solid #0090e0; }QCheckBox::indicator:checked { background: #00b4d8; border: "
     "1px solid #40c8e8; }QCheckBox::indicator:disabled { background: #1a2028; border: 1px "
     "solid #252f3a; }",
     /*themed*/
     "QCheckBox { spacing: 0px; }QCheckBox::indicator { width: 12px; height: 12px; border: 1px "
     "solid {{color.button.overlay.ant.check.border}}; border-radius: 2px; background: "
     "{{color.button.overlay.ant.check.background}}; }QCheckBox::indicator:hover { border: 1px "
     "solid {{color.button.overlay.ant.check.hover.border}}; }QCheckBox::indicator:checked { "
     "background: {{color.button.overlay.ant.check.checked.background}}; border: 1px solid "
     "{{color.button.overlay.ant.check.checked.border}}; }QCheckBox::indicator:disabled { "
     "background: {{color.button.overlay.ant.check.disabled.background}}; border: 1px solid "
     "{{color.button.overlay.ant.check.disabled.border}}; }"},

    {"overlay.frontend", "SpectrumOverlayMenu.cpp: front end PRE / ATT buttons (kFrontEndBtnStyle)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; border: 1px solid "
     "{{color.background.2}}; border-radius: 3px; color: {{color.text.secondary}}; font-size: "
     "11px; padding: 1px 4px; }QPushButton:hover { border-color: {{color.accent.bright}}; "
     "}QPushButton:checked { background: {{color.accent.dim}}; border-color: "
     "{{color.accent.bright}}; color: {{color.text.primary}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.overlay.frontend.background}}; border: 1px "
     "solid {{color.button.overlay.frontend.border}}; border-radius: 3px; color: "
     "{{color.button.overlay.frontend.text}}; font-size: 11px; padding: 1px 4px; "
     "}QPushButton:hover { border-color: {{color.button.overlay.frontend.hover.border}}; "
     "}QPushButton:checked { background: {{color.button.overlay.frontend.checked.background}}; "
     "border-color: {{color.button.overlay.frontend.checked.border}}; color: "
     "{{color.button.overlay.frontend.checked.text}}; }"},

    {"overlay.wnb", "SpectrumOverlayMenu.cpp: WNB",
     /*original*/
     "QPushButton { background: {{color.background.1}}; border: 1px solid "
     "{{color.background.2}}; border-radius: 2px; color: {{color.text.primary}}; font-size: "
     "11px; font-weight: bold; }QPushButton:checked { background: {{color.background.2}}; "
     "color: {{color.text.primary}}; border: 1px solid {{color.accent.dim}}; "
     "}QPushButton:hover { border: 1px solid {{color.accent.dim}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.overlay.wnb.background}}; border: 1px solid "
     "{{color.button.overlay.wnb.border}}; border-radius: 2px; color: "
     "{{color.button.overlay.wnb.text}}; font-size: 11px; font-weight: bold; "
     "}QPushButton:checked { background: {{color.button.overlay.wnb.checked.background}}; "
     "color: {{color.button.overlay.wnb.checked.text}}; border: 1px solid "
     "{{color.button.overlay.wnb.checked.border}}; }QPushButton:hover { border: 1px solid "
     "{{color.button.overlay.wnb.hover.border}}; }"},

    {"overlay.wfm", "SpectrumOverlayMenu.cpp: WFM (three-digit colours: not remapped by the remap themes)",
     /*original*/
     "QPushButton { background: #444; color: #ccc; border: 1px solid #666; border-radius: 3px; "
     "font-size: 11px; font-weight: bold; padding: 2px 8px; }QPushButton:checked { background: "
     "#2a7; color: #fff; border-color: #2a7; }QPushButton:hover { background: #555; }",
     /*themed*/
     "QPushButton { background: {{color.button.overlay.wfm.background}}; color: "
     "{{color.button.overlay.wfm.text}}; border: 1px solid "
     "{{color.button.overlay.wfm.border}}; border-radius: 3px; font-size: 11px; font-weight: "
     "bold; padding: 2px 8px; }QPushButton:checked { background: "
     "{{color.button.overlay.wfm.checked.background}}; color: "
     "{{color.button.overlay.wfm.checked.text}}; border-color: "
     "{{color.button.overlay.wfm.checked.border}}; }QPushButton:hover { background: "
     "{{color.button.overlay.wfm.hover.background}}; }"},

    {"overlay.display", "SpectrumOverlayMenu.cpp: Display panel buttons -- trace / waterfall options, background, Clone / Reset (btnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #205070; "
     "border-radius: 3px; font-size: 10px; font-weight: bold; padding: 2px 6px; "
     "}QPushButton:hover { background: #204060; }QPushButton:checked { background: #006040; "
     "color: #00ff88; border-color: #00a060; }",
     /*themed*/
     "QPushButton { background: {{color.button.overlay.display.background}}; color: "
     "{{color.button.overlay.display.text}}; border: 1px solid "
     "{{color.button.overlay.display.border}}; border-radius: 3px; font-size: 10px; "
     "font-weight: bold; padding: 2px 6px; }QPushButton:hover { background: "
     "{{color.button.overlay.display.hover.background}}; }QPushButton:checked { background: "
     "{{color.button.overlay.display.checked.background}}; color: "
     "{{color.button.overlay.display.checked.text}}; border-color: "
     "{{color.button.overlay.display.checked.border}}; }"},

    // ── Shack switch applet (build 778)
    {"shack.idle", "ShackSwitchApplet.cpp: A / B antenna buttons at rest (kABtnNeutral = kBBtnNeutral)",
     /*original*/
     "QPushButton { background: #0b1220; border: 1px solid #1c2a40; border-radius: 3px; "
     "padding: 3px 6px; font-size: 10px; font-weight: bold; color: #6b8099; min-width: 18px; "
     "}QPushButton:hover { background: #0e1a2e; }",
     /*themed*/
     "QPushButton { background: {{color.button.shack.background}}; border: 1px solid "
     "{{color.button.shack.border}}; border-radius: 3px; padding: 3px 6px; font-size: 10px; "
     "font-weight: bold; color: {{color.button.shack.text}}; min-width: 18px; "
     "}QPushButton:hover { background: {{color.button.shack.hover.background}}; }"},

    {"shack.a.active", "ShackSwitchApplet.cpp: A button on its antenna (kABtnActive)",
     /*original*/
     "QPushButton { background: #041e2a; border: 1px solid #00d8ef; border-radius: 3px; "
     "padding: 3px 6px; font-size: 10px; font-weight: bold; color: #00d8ef; min-width: 18px; }",
     /*themed*/
     "QPushButton { background: {{color.button.shack.a.active.background}}; border: 1px solid "
     "{{color.button.shack.a.active.border}}; border-radius: 3px; padding: 3px 6px; font-size: "
     "10px; font-weight: bold; color: {{color.button.shack.a.active.text}}; min-width: 18px; }"},

    {"shack.b.active", "ShackSwitchApplet.cpp: B button on its antenna (kBBtnActive)",
     /*original*/
     "QPushButton { background: #1e0e00; border: 1px solid #f97316; border-radius: 3px; "
     "padding: 3px 6px; font-size: 10px; font-weight: bold; color: #f97316; min-width: 18px; }",
     /*themed*/
     "QPushButton { background: {{color.button.shack.b.active.background}}; border: 1px solid "
     "{{color.button.shack.b.active.border}}; border-radius: 3px; padding: 3px 6px; font-size: "
     "10px; font-weight: bold; color: {{color.button.shack.b.active.text}}; min-width: 18px; }"},

    {"shack.b.conflict", "ShackSwitchApplet.cpp: B button blinking on a conflict (kBBtnConflict)",
     /*original*/
     "QPushButton { background: #1e1200; border: 1px solid #ffaa00; border-radius: 3px; "
     "padding: 3px 6px; font-size: 10px; font-weight: bold; color: #ffaa00; min-width: 18px; }",
     /*themed*/
     "QPushButton { background: {{color.button.shack.b.conflict.background}}; border: 1px "
     "solid {{color.button.shack.b.conflict.border}}; border-radius: 3px; padding: 3px 6px; "
     "font-size: 10px; font-weight: bold; color: {{color.button.shack.b.conflict.text}}; "
     "min-width: 18px; }"},

    {"shack.b.parked", "ShackSwitchApplet.cpp: B button parked on the dummy load, dim half of the blink (kBBtnParked)",
     /*original*/
     "QPushButton { background: #0b1220; border: 1px solid #c05000; border-radius: 3px; "
     "padding: 3px 6px; font-size: 10px; font-weight: bold; color: #c05000; min-width: 18px; }",
     /*themed*/
     "QPushButton { background: {{color.button.shack.b.parked.background}}; border: 1px solid "
     "{{color.button.shack.b.parked.border}}; border-radius: 3px; padding: 3px 6px; font-size: "
     "10px; font-weight: bold; color: {{color.button.shack.b.parked.text}}; min-width: 18px; }"},

    {"shack.settings", "ShackSwitchApplet.cpp: Settings (kSettingsBtnStyle)",
     /*original*/
     "QPushButton { background: #0f1e30; border: 1px solid #008fa0; border-radius: 3px; "
     "padding: 4px 10px; font-size: 10px; font-weight: bold; color: #00d8ef; "
     "}QPushButton:hover { background: #041e2a; }",
     /*themed*/
     "QPushButton { background: {{color.button.shack.settings.background}}; border: 1px solid "
     "{{color.button.shack.settings.border}}; border-radius: 3px; padding: 4px 10px; "
     "font-size: 10px; font-weight: bold; color: {{color.button.shack.settings.text}}; "
     "}QPushButton:hover { background: {{color.button.shack.settings.hover.background}}; }"},

    {"shack.dummy", "ShackSwitchApplet.cpp: dummy load button (kDummyLoadBtnStyle)",
     /*original*/
     "QPushButton { background: #0b1220; border: 1px solid #1c2a40; border-radius: 3px; "
     "padding: 4px 8px; font-size: 10px; color: #6b8099; text-align: left; }QPushButton:hover "
     "{ background: #0e1a2e; color: #dde6f0; }",
     /*themed*/
     "QPushButton { background: {{color.button.shack.background}}; border: 1px solid "
     "{{color.button.shack.border}}; border-radius: 3px; padding: 4px 8px; font-size: 10px; "
     "color: {{color.button.shack.text}}; text-align: left; }QPushButton:hover { background: "
     "{{color.button.shack.hover.background}}; color: {{color.button.shack.dummy.hover.text}}; "
     "}"},

    {"shack.dummy.active", "ShackSwitchApplet.cpp: dummy load button, on (kDummyLoadBtnActiveStyle)",
     /*original*/
     "QPushButton { background: #0b1220; border: 1px solid #c05000; border-radius: 3px; "
     "padding: 4px 8px; font-size: 10px; color: #f97316; text-align: left; }QPushButton:hover "
     "{ background: #1e0e00; }",
     /*themed*/
     "QPushButton { background: {{color.button.shack.background}}; border: 1px solid "
     "{{color.button.shack.dummy.active.border}}; border-radius: 3px; padding: 4px 8px; "
     "font-size: 10px; color: {{color.button.shack.dummy.active.text}}; text-align: left; "
     "}QPushButton:hover { background: {{color.button.shack.dummy.active.hover.background}}; }"},

    // ── Antenna Genius applet (build 779)
    {"ag.connect", "AntennaGeniusApplet.cpp: Connect (kButtonBase + bold)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #203040; border-radius: 3px; "
     "padding: 2px 2px; font-size: 10px; color: #c8d8e8; }QPushButton:hover { background: "
     "#243848; }QPushButton { font-size: 10px; font-weight: bold; }",
     /*themed*/
     "QPushButton { background: {{color.button.ag.background}}; border: 1px solid "
     "{{color.button.ag.border}}; border-radius: 3px; padding: 2px 2px; font-size: 10px; "
     "color: {{color.button.ag.text}}; }QPushButton:hover { background: "
     "{{color.button.ag.hover.background}}; }QPushButton { font-size: 10px; font-weight: bold; "
     "}"},

    {"ag.auto", "AntennaGeniusApplet.cpp: port A / B Auto (kButtonBase + kGreenActive)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #203040; border-radius: 3px; "
     "padding: 2px 2px; font-size: 10px; color: #c8d8e8; }QPushButton:hover { background: "
     "#243848; }QPushButton:checked { background-color: #006040; color: #00ff88; border: 1px "
     "solid #00a060; }",
     /*themed*/
     "QPushButton { background: {{color.button.ag.background}}; border: 1px solid "
     "{{color.button.ag.border}}; border-radius: 3px; padding: 2px 2px; font-size: 10px; "
     "color: {{color.button.ag.text}}; }QPushButton:hover { background: "
     "{{color.button.ag.hover.background}}; }QPushButton:checked { background-color: "
     "{{color.button.ag.checked.background}}; color: {{color.button.ag.checked.text}}; border: "
     "1px solid {{color.button.ag.checked.border}}; }"},

    {"ag.ant", "AntennaGeniusApplet.cpp: antenna buttons, TX + RX or band unknown (kButtonBase + kBlueActive)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #203040; border-radius: 3px; "
     "padding: 2px 2px; font-size: 10px; color: #c8d8e8; }QPushButton:hover { background: "
     "#243848; }QPushButton:checked { background-color: #0070c0; color: #ffffff; border: 1px "
     "solid #0090e0; }",
     /*themed*/
     "QPushButton { background: {{color.button.ag.background}}; border: 1px solid "
     "{{color.button.ag.border}}; border-radius: 3px; padding: 2px 2px; font-size: 10px; "
     "color: {{color.button.ag.text}}; }QPushButton:hover { background: "
     "{{color.button.ag.hover.background}}; }QPushButton:checked { background-color: "
     "{{color.button.ag.selected.background}}; color: {{color.button.ag.selected.text}}; "
     "border: 1px solid {{color.button.ag.selected.border}}; }"},

    {"ag.ant.rxonly", "AntennaGeniusApplet.cpp: antenna buttons, RX only on this band (kButtonBase + kAmberActive)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #203040; border-radius: 3px; "
     "padding: 2px 2px; font-size: 10px; color: #c8d8e8; }QPushButton:hover { background: "
     "#243848; }QPushButton:checked { background-color: #8a6000; color: #ffe080; border: 1px "
     "solid #a07000; }",
     /*themed*/
     "QPushButton { background: {{color.button.ag.background}}; border: 1px solid "
     "{{color.button.ag.border}}; border-radius: 3px; padding: 2px 2px; font-size: 10px; "
     "color: {{color.button.ag.text}}; }QPushButton:hover { background: "
     "{{color.button.ag.hover.background}}; }QPushButton:checked { background-color: "
     "{{color.button.ag.rxonly.checked.background}}; color: "
     "{{color.button.ag.rxonly.checked.text}}; border: 1px solid "
     "{{color.button.ag.rxonly.checked.border}}; }"},

    {"ag.ant.dim", "AntennaGeniusApplet.cpp: antenna buttons in use by the other port / no RX or TX on this band",
     /*original*/
     "QPushButton { background: #101820; border: 1px solid #182028; border-radius: 3px; "
     "padding: 2px 2px; font-size: 10px; color: {{color.background.2}}; }QPushButton:checked { "
     "background: #202830; color: #606878; border: 1px solid #303848; }",
     /*themed*/
     "QPushButton { background: {{color.button.ag.dim.background}}; border: 1px solid "
     "{{color.button.ag.dim.border}}; border-radius: 3px; padding: 2px 2px; font-size: 10px; "
     "color: {{color.button.ag.dim.text}}; }QPushButton:checked { background: "
     "{{color.button.ag.dim.checked.background}}; color: {{color.button.ag.dim.checked.text}}; "
     "border: 1px solid {{color.button.ag.dim.checked.border}}; }"},

    // ── Spot settings dialog (build 779)
    {"spots.toggle", "SpotSettingsDialog.cpp: Enabled / Disabled toggles (spots, memories, colour override, background, Auto, spot lines)",
     /*original*/
     "QPushButton { background: #206030; color: white; border: 1px solid #305040; padding: "
     "3px; }QPushButton:!checked { background: #603020; }",
     /*themed*/
     "QPushButton { background: {{color.button.spots.on.background}}; color: white; border: "
     "1px solid {{color.button.spots.border}}; padding: 3px; }QPushButton:!checked { "
     "background: {{color.button.spots.off.background}}; }"},

    // ── Client chain applet (build 780)
    {"chain.mode", "ClientChainApplet.cpp: TX / RX chain select (kModeBtnStyle)",
     /*original*/
     "QPushButton {  background: #1a2a3a; border: 1px solid #2a4458; border-radius: 3px;  "
     "color: #8aa8c0; font-size: 10px; font-weight: bold;  padding: 2px 10px; min-width: "
     "30px;}QPushButton:hover { background: #24384e; }QPushButton:checked {  background: "
     "#3a2a0e; color: #f2c14e; border: 1px solid #f2c14e;}",
     /*themed*/
     "QPushButton {  background: {{color.button.chain.background}}; border: 1px solid "
     "{{color.button.chain.border}}; border-radius: 3px;  color: {{color.button.chain.text}}; "
     "font-size: 10px; font-weight: bold;  padding: 2px 10px; min-width: "
     "30px;}QPushButton:hover { background: {{color.button.chain.hover.background}}; "
     "}QPushButton:checked {  background: {{color.button.chain.checked.background}}; color: "
     "{{color.button.chain.checked.text}}; border: 1px solid "
     "{{color.button.chain.checked.border}};}"},

    {"chain.bypass", "ClientChainApplet.cpp: BYPASS (kBypassBtnStyle)",
     /*original*/
     "QPushButton {  background: #1a2a3a; border: 1px solid #4a3020; border-radius: 3px;  "
     "color: #c8a070; font-size: 10px; font-weight: bold;  padding: 2px "
     "10px;}QPushButton:hover { background: #3a2818; color: #f2c14e;                    "
     "border: 1px solid #f2c14e; }QPushButton:checked {  background: #4a3818; color: #f2c14e; "
     "border: 1px solid #f2c14e;}QPushButton:checked:hover { background: #5a4a28; }",
     /*themed*/
     "QPushButton {  background: {{color.button.chain.background}}; border: 1px solid "
     "{{color.button.chain.bypass.border}}; border-radius: 3px;  color: "
     "{{color.button.chain.bypass.text}}; font-size: 10px; font-weight: bold;  padding: 2px "
     "10px;}QPushButton:hover { background: {{color.button.chain.bypass.hover.background}}; "
     "color: {{color.button.chain.bypass.hover.text}};                    border: 1px solid "
     "{{color.button.chain.bypass.hover.border}}; }QPushButton:checked {  background: "
     "{{color.button.chain.bypass.checked.background}}; color: "
     "{{color.button.chain.bypass.checked.text}}; border: 1px solid "
     "{{color.button.chain.bypass.checked.border}};}QPushButton:checked:hover { background: "
     "{{color.button.chain.bypass.checked.hover.background}}; }"},

    {"chain.rec.idle", "ClientChainApplet.cpp: PUDU monitor rec button, idle (kMonBtnBase + kMonRecIdle)",
     /*original*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: #303030;  background: "
     "rgba(255,255,255,5); }QPushButton:enabled { color: #804040; }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: "
     "{{color.button.chain.mon.disabled.text}};  background: rgba(255,255,255,5); "
     "}QPushButton:enabled { color: {{color.button.chain.rec.text}}; }"},

    {"chain.rec.bright", "ClientChainApplet.cpp: PUDU monitor rec button, active, bright half of the pulse",
     /*original*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: #303030;  background: "
     "rgba(255,255,255,5); }QPushButton {  color: #ff2020; background: rgba(255,50,50,60); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: "
     "{{color.button.chain.mon.disabled.text}};  background: rgba(255,255,255,5); }QPushButton "
     "{  color: {{color.button.chain.rec.active.text}}; background: rgba(255,50,50,60); }"},

    {"chain.rec.dim", "ClientChainApplet.cpp: PUDU monitor rec button, active, dim half of the pulse",
     /*original*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: #303030;  background: "
     "rgba(255,255,255,5); }QPushButton {  color: #601010; background: rgba(255,50,50,20); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: "
     "{{color.button.chain.mon.disabled.text}};  background: rgba(255,255,255,5); }QPushButton "
     "{  color: {{color.button.chain.rec.active.dim.text}}; background: rgba(255,50,50,20); }"},

    {"chain.play.idle", "ClientChainApplet.cpp: PUDU monitor play button, idle (kMonBtnBase + kMonPlayIdle)",
     /*original*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: #303030;  background: "
     "rgba(255,255,255,5); }QPushButton:enabled { color: #406040; }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: "
     "{{color.button.chain.mon.disabled.text}};  background: rgba(255,255,255,5); "
     "}QPushButton:enabled { color: {{color.button.chain.play.text}}; }"},

    {"chain.play.bright", "ClientChainApplet.cpp: PUDU monitor play button, active, bright half of the pulse",
     /*original*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: #303030;  background: "
     "rgba(255,255,255,5); }QPushButton {  color: #30d050; background: rgba(50,200,80,60); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: "
     "{{color.button.chain.mon.disabled.text}};  background: rgba(255,255,255,5); }QPushButton "
     "{  color: {{color.button.chain.play.active.text}}; background: rgba(50,200,80,60); }"},

    {"chain.play.dim", "ClientChainApplet.cpp: PUDU monitor play button, active, dim half of the pulse",
     /*original*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: #303030;  background: "
     "rgba(255,255,255,5); }QPushButton {  color: #103010; background: rgba(50,200,80,20); }",
     /*themed*/
     "QPushButton { background: rgba(255,255,255,15); border: none;  border-radius: 10px; "
     "font-size: 11px; padding: 0; }QPushButton:hover:enabled { background: "
     "rgba(255,255,255,40); }QPushButton:disabled { color: "
     "{{color.button.chain.mon.disabled.text}};  background: rgba(255,255,255,5); }QPushButton "
     "{  color: {{color.button.chain.play.active.dim.text}}; background: rgba(50,200,80,20); }"},

    // ── Tuner applet (build 780)
    {"tuner.tune.busy", "TunerApplet.cpp: TUNE while tuning, rail and panel (kTuneBusyStyle; the rail adds its font size)",
     /*original*/
     "QPushButton { background: #cc2222; border: 1px solid {{color.accent.danger}}; "
     "border-radius: 3px; color: {{color.text.primary}}; font-weight: bold; }",
     /*themed*/
     "QPushButton { background: {{color.button.tuner.tune.busy.background}}; border: 1px solid "
     "{{color.button.tuner.tune.busy.border}}; border-radius: 3px; color: "
     "{{color.button.tuner.tune.busy.text}}; font-weight: bold; }"},

    {"tuner.operate", "TunerApplet.cpp: rail OPERATE key, operating (the rail adds its font size)",
     /*original*/
     "QPushButton { background: #006030; border: 1px solid #008040; border-radius: 3px; color: "
     "{{color.text.primary}}; font-weight: bold; }QPushButton:hover { background: #007040; }",
     /*themed*/
     "QPushButton { background: {{color.button.tuner.operate.background}}; border: 1px solid "
     "{{color.button.tuner.operate.border}}; border-radius: 3px; color: "
     "{{color.button.tuner.operate.text}}; font-weight: bold; }QPushButton:hover { background: "
     "{{color.button.tuner.operate.hover.background}}; }"},

    {"tuner.bypass", "TunerApplet.cpp: rail OPERATE key showing BYPASS (the rail adds its font size)",
     /*original*/
     "QPushButton { background: #8a6000; border: 1px solid #a07000; border-radius: 3px; color: "
     "{{color.text.primary}}; font-weight: bold; }QPushButton:hover { background: #9a7000; }",
     /*themed*/
     "QPushButton { background: {{color.button.tuner.bypass.background}}; border: 1px solid "
     "{{color.button.tuner.bypass.border}}; border-radius: 3px; color: "
     "{{color.button.tuner.bypass.text}}; font-weight: bold; }QPushButton:hover { background: "
     "{{color.button.tuner.bypass.hover.background}}; }"},

    {"tuner.ant", "TunerApplet.cpp: ANT1-3 (updateAntennaButtons kDefault)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #205070; border-radius: 3px; color: "
     "#c8d8e8; font-size: 10px; font-weight: bold; }QPushButton:hover { background: #204060; }",
     /*themed*/
     "QPushButton { background: {{color.button.tuner.ant.background}}; border: 1px solid "
     "{{color.button.tuner.ant.border}}; border-radius: 3px; color: "
     "{{color.button.tuner.ant.text}}; font-size: 10px; font-weight: bold; }QPushButton:hover "
     "{ background: {{color.button.tuner.ant.hover.background}}; }"},

    {"tuner.ant.active", "TunerApplet.cpp: the selected ANT (updateAntennaButtons kActive)",
     /*original*/
     "QPushButton { background: #006030; border: 1px solid #008040; border-radius: 3px; color: "
     "#ffffff; font-size: 10px; font-weight: bold; }",
     /*themed*/
     "QPushButton { background: {{color.button.tuner.ant.active.background}}; border: 1px "
     "solid {{color.button.tuner.ant.active.border}}; border-radius: 3px; color: "
     "{{color.button.tuner.ant.active.text}}; font-size: 10px; font-weight: bold; }"},

    // ── RX applet +RX and TX badge, PureSignal applet, panadapter decoder bars (build 781)
    {"rx.addrx", "RxApplet.cpp: +RX after the receiver buttons (ANAN)",
     /*original*/
     "QToolButton { background: transparent; color: #c8d8e8; border: 1px solid #6a7a8a; "
     "border-radius: 3px; font-size: 10px; padding: 0; }QToolButton:hover { color: #ffffff; "
     "border-color: #c8d8e8; }QToolButton:pressed { background: #2a3a4a; }",
     /*themed*/
     "QToolButton { background: transparent; color: {{color.button.rx.addrx.text}}; border: "
     "1px solid {{color.button.rx.addrx.border}}; border-radius: 3px; font-size: 10px; "
     "padding: 0; }QToolButton:hover { color: {{color.button.rx.addrx.hover.text}}; "
     "border-color: {{color.button.rx.addrx.hover.border}}; }QToolButton:pressed { background: "
     "{{color.button.rx.addrx.pressed.background}}; }"},

    {"rx.badge.tx", "RxApplet.cpp: TX badge, the transmit slice",
     /*original*/
     "QPushButton { background: #c03030; color: #ffffff; border-radius: 3px; border: none; "
     "font-weight: bold; font-size: 10px; padding: 0px; margin: 0px; }QPushButton:hover { "
     "background: #d04040; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.badge.tx.background}}; color: "
     "{{color.button.rx.badge.text}}; border-radius: 3px; border: none; font-weight: bold; "
     "font-size: 10px; padding: 0px; margin: 0px; }QPushButton:hover { background: "
     "{{color.button.rx.badge.tx.hover.background}}; }"},

    {"rx.badge.rx", "RxApplet.cpp: TX badge, not the transmit slice",
     /*original*/
     "QPushButton { background: #405060; color: #ffffff; border-radius: 3px; border: none; "
     "font-weight: bold; font-size: 10px; padding: 0px; margin: 0px; }QPushButton:hover { "
     "background: #506070; }",
     /*themed*/
     "QPushButton { background: {{color.button.rx.badge.rx.background}}; color: "
     "{{color.button.rx.badge.text}}; border-radius: 3px; border: none; font-weight: bold; "
     "font-size: 10px; padding: 0px; margin: 0px; }QPushButton:hover { background: "
     "{{color.button.rx.badge.rx.hover.background}}; }"},

    {"ps.button", "PsApplet.cpp: Auto, Single, Calibrate, Reset, Amp View (kBtnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; color: "
     "#c8d8e8; font-size: 11px; font-weight: bold; padding: 1px 4px; }QPushButton:hover { "
     "border: 1px solid #0090e0; }QPushButton:pressed { background: #0a5a90; border: 1px solid "
     "#40b0ff; color: #ffffff; }QPushButton:checked { background: #0070c0; border: 1px solid "
     "#0090e0; color: #ffffff; }QPushButton:disabled { color: #607080; }",
     /*themed*/
     "QPushButton { background: {{color.button.ps.background}}; border: 1px solid "
     "{{color.button.ps.border}}; border-radius: 2px; color: {{color.button.ps.text}}; "
     "font-size: 11px; font-weight: bold; padding: 1px 4px; }QPushButton:hover { border: 1px "
     "solid {{color.button.ps.hover.border}}; }QPushButton:pressed { background: "
     "{{color.button.ps.pressed.background}}; border: 1px solid "
     "{{color.button.ps.pressed.border}}; color: {{color.button.ps.pressed.text}}; "
     "}QPushButton:checked { background: {{color.button.ps.selected.background}}; border: 1px "
     "solid {{color.button.ps.selected.border}}; color: {{color.button.ps.selected.text}}; "
     "}QPushButton:disabled { color: {{color.button.ps.disabled.text}}; }"},

    {"ps.ps", "PsApplet.cpp: PS (kBtnStyle + kPsOnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; color: "
     "#c8d8e8; font-size: 11px; font-weight: bold; padding: 1px 4px; }QPushButton:hover { "
     "border: 1px solid #0090e0; }QPushButton:pressed { background: #0a5a90; border: 1px solid "
     "#40b0ff; color: #ffffff; }QPushButton:checked { background: #0070c0; border: 1px solid "
     "#0090e0; color: #ffffff; }QPushButton:disabled { color: #607080; }QPushButton:checked { "
     "background: #1d8a3a; border: 1px solid #30c060; color: #ffffff; }",
     /*themed*/
     "QPushButton { background: {{color.button.ps.background}}; border: 1px solid "
     "{{color.button.ps.border}}; border-radius: 2px; color: {{color.button.ps.text}}; "
     "font-size: 11px; font-weight: bold; padding: 1px 4px; }QPushButton:hover { border: 1px "
     "solid {{color.button.ps.hover.border}}; }QPushButton:pressed { background: "
     "{{color.button.ps.pressed.background}}; border: 1px solid "
     "{{color.button.ps.pressed.border}}; color: {{color.button.ps.pressed.text}}; "
     "}QPushButton:checked { background: {{color.button.ps.selected.background}}; border: 1px "
     "solid {{color.button.ps.selected.border}}; color: {{color.button.ps.selected.text}}; "
     "}QPushButton:disabled { color: {{color.button.ps.disabled.text}}; }QPushButton:checked { "
     "background: {{color.button.ps.on.background}}; border: 1px solid "
     "{{color.button.ps.on.border}}; color: {{color.button.ps.on.text}}; }"},

    {"ps.twotone", "PsApplet.cpp: 2 Tone (kBtnStyle + kTwoToneOnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; color: "
     "#c8d8e8; font-size: 11px; font-weight: bold; padding: 1px 4px; }QPushButton:hover { "
     "border: 1px solid #0090e0; }QPushButton:pressed { background: #0a5a90; border: 1px solid "
     "#40b0ff; color: #ffffff; }QPushButton:checked { background: #0070c0; border: 1px solid "
     "#0090e0; color: #ffffff; }QPushButton:disabled { color: #607080; }QPushButton:checked { "
     "background: #c03030; border: 1px solid #ff5050; color: #ffffff; }",
     /*themed*/
     "QPushButton { background: {{color.button.ps.background}}; border: 1px solid "
     "{{color.button.ps.border}}; border-radius: 2px; color: {{color.button.ps.text}}; "
     "font-size: 11px; font-weight: bold; padding: 1px 4px; }QPushButton:hover { border: 1px "
     "solid {{color.button.ps.hover.border}}; }QPushButton:pressed { background: "
     "{{color.button.ps.pressed.background}}; border: 1px solid "
     "{{color.button.ps.pressed.border}}; color: {{color.button.ps.pressed.text}}; "
     "}QPushButton:checked { background: {{color.button.ps.selected.background}}; border: 1px "
     "solid {{color.button.ps.selected.border}}; color: {{color.button.ps.selected.text}}; "
     "}QPushButton:disabled { color: {{color.button.ps.disabled.text}}; }QPushButton:checked { "
     "background: {{color.button.ps.twotone.background}}; border: 1px solid "
     "{{color.button.ps.twotone.border}}; color: {{color.button.ps.twotone.text}}; }"},

    {"pan.decoder", "PanadapterApplet.cpp: CW / RTTY decoder bar buttons -- copy, font size, clear (cwBtnStyle = rttBtnStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; color: #8090a0; border: 1px solid #203040; "
     "border-radius: 2px; font-size: 9px; font-weight: bold; padding: 1px 6px; "
     "}QPushButton:hover { color: #c8d8e8; background: #2a3a4a; }",
     /*themed*/
     "QPushButton { background: {{color.button.pan.decoder.background}}; color: "
     "{{color.button.pan.decoder.text}}; border: 1px solid "
     "{{color.button.pan.decoder.border}}; border-radius: 2px; font-size: 9px; font-weight: "
     "bold; padding: 1px 6px; }QPushButton:hover { color: "
     "{{color.button.pan.decoder.hover.text}}; background: "
     "{{color.button.pan.decoder.hover.background}}; }"},

    {"pan.decoder.close", "PanadapterApplet.cpp: CW / RTTY decoder close",
     /*original*/
     "QPushButton { background: {{color.background.1}}; color: {{color.text.secondary}}; "
     "border: 1px solid {{color.background.1}}; border-radius: 2px; font-size: 9px; "
     "font-weight: bold; padding: 1px 6px; }QPushButton:hover { color: #ff6060; background: "
     "{{color.background.1}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.pan.close.background}}; color: "
     "{{color.button.pan.close.text}}; border: 1px solid {{color.button.pan.close.border}}; "
     "border-radius: 2px; font-size: 9px; font-weight: bold; padding: 1px 6px; "
     "}QPushButton:hover { color: {{color.button.pan.close.hover.text}}; background: "
     "{{color.button.pan.close.hover.background}}; }"},

    // ── FtPanelTx.cpp (group ftpanel)
    {"ftpanel.pb.enable", "FtPanelTx.cpp: pbEnable",
     /*original*/
     "QPushButton { padding: 2px 5px; } QPushButton:checked { background: #A02020; "
     "border-color: #FF8080; }",
     /*themed*/
     "QPushButton { padding: 2px 5px; } QPushButton:checked { background: "
     "{{color.button.ftpanel.checked.background}}; border-color: "
     "{{color.button.ftpanel.checked.border}}; }"},

    // ── Js8PanelTx.cpp (group js8panel)
    {"js8panel.pb.auto", "Js8PanelTx.cpp: pbAuto",
     /*original*/
     "QPushButton:checked { background: #6F5A1F; border-color: #E0C080; }",
     /*themed*/
     "QPushButton:checked { background: {{color.button.js8panel.checked.background}}; "
     "border-color: {{color.button.js8panel.checked.border}}; }"},

    {"js8panel.pb.hb", "Js8PanelTx.cpp: pbHb",
     /*original*/
     "QPushButton:checked { background: #1F4F6F; border-color: #80C0E0; }",
     /*themed*/
     "QPushButton:checked { background: {{color.button.js8panel.pb.hb.checked.background}}; "
     "border-color: {{color.button.js8panel.pb.hb.checked.border}}; }"},

    {"js8panel.pb.send", "Js8PanelTx.cpp: pbSend",
     /*original*/
     "QPushButton { background: #12466a; border: 1px solid #6a90b0; font-weight: bold; "
     "}QPushButton:disabled { background: #303030; color: #808080; border-color: #505050; }",
     /*themed*/
     "QPushButton { background: {{color.button.js8panel.background}}; border: 1px solid "
     "{{color.button.js8panel.border}}; font-weight: bold; }QPushButton:disabled { background: "
     "{{color.button.js8panel.disabled.background}}; color: "
     "{{color.button.js8panel.disabled.text}}; border-color: "
     "{{color.button.js8panel.disabled.border}}; }"},

    // ── AcomApplet.cpp (group acom)
    {"acom.temp", "AcomApplet.cpp: m_tempBtn",
     /*original*/
     "QPushButton { background: transparent; border: 1px solid transparent; color: #c8d8e8; "
     "font-size: 10px; text-align: left; padding: 0 2px; }QPushButton:hover { border-color: "
     "#203040; color: #ffffff; }QPushButton:focus { border-color: #00b4d8; }",
     /*themed*/
     "QPushButton { background: transparent; border: 1px solid transparent; color: "
     "{{color.button.acom.text}}; font-size: 10px; text-align: left; padding: 0 2px; "
     "}QPushButton:hover { border-color: {{color.button.acom.hover.border}}; color: "
     "{{color.button.acom.hover.text}}; }QPushButton:focus { border-color: "
     "{{color.button.acom.focus.border}}; }"},

    // ── BandStackPanel.cpp (group bandstack)
    {"bandstack.small.btn", "BandStackPanel.cpp: m_clearAllButton",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 3px; color: "
     "#8aa8c0; font-size: 13px; font-weight: bold; padding: 0px; margin: 0px; "
     "}QPushButton:hover { background: #203040; color: #c8d8e8; }",
     /*themed*/
     "QPushButton { background: {{color.button.bandstack.background}}; border: 1px solid "
     "{{color.button.bandstack.border}}; border-radius: 3px; color: "
     "{{color.button.bandstack.text}}; font-size: 13px; font-weight: bold; padding: 0px; "
     "margin: 0px; }QPushButton:hover { background: "
     "{{color.button.bandstack.hover.background}}; color: "
     "{{color.button.bandstack.hover.text}}; }"},

    // ── ClientEqEditor.cpp (group eqeditor)
    {"eqeditor.peak.hold", "ClientEqEditor.cpp: peakHoldBtn",
     /*original*/
     "QPushButton {  background: {{color.background.0}}; color: {{color.text.primary}};  "
     "border: 1px solid {{color.background.1}}; border-radius: 3px;  padding: 2px 12px; "
     "font-size: 11px; font-weight: bold;}QPushButton:hover { background: "
     "{{color.background.1}}; }QPushButton:checked {  background: #c8a040; color: "
     "{{color.background.0}};  border-color: #d4b050;}QPushButton:checked:hover { background: "
     "#d4b050; }",
     /*themed*/
     "QPushButton {  background: {{color.background.0}}; color: {{color.text.primary}};  "
     "border: 1px solid {{color.background.1}}; border-radius: 3px;  padding: 2px 12px; "
     "font-size: 11px; font-weight: bold;}QPushButton:hover { background: "
     "{{color.background.1}}; }QPushButton:checked {  background: "
     "{{color.button.eqeditor.checked.background}}; color: {{color.background.0}};  "
     "border-color: {{color.button.eqeditor.checked.border}};}QPushButton:checked:hover { "
     "background: {{color.button.eqeditor.checked.hover.background}}; }"},

    // ── ClientGateEditor.cpp (group gateeditor)
    {"gateeditor.flip", "ClientGateEditor.cpp: m_flip",
     /*original*/
     "QPushButton {  background: #006040; border: 1px solid #00a060; border-radius: 3px;  "
     "color: #00ff88; font-size: 10px; font-weight: bold; padding: 3px 6px;}QPushButton:hover "
     "{ background: #007050; }QPushButton:checked {  background: #3a2a0e; color: #f2c14e; "
     "border: 1px solid #f2c14e;}QPushButton:checked:hover { background: #4a3a1e; }",
     /*themed*/
     "QPushButton {  background: {{color.button.gateeditor.background}}; border: 1px solid "
     "{{color.button.gateeditor.border}}; border-radius: 3px;  color: "
     "{{color.button.gateeditor.text}}; font-size: 10px; font-weight: bold; padding: 3px "
     "6px;}QPushButton:hover { background: {{color.button.gateeditor.hover.background}}; "
     "}QPushButton:checked {  background: {{color.button.gateeditor.checked.background}}; "
     "color: {{color.button.gateeditor.checked.text}}; border: 1px solid "
     "{{color.button.gateeditor.checked.border}};}QPushButton:checked:hover { background: "
     "{{color.button.gateeditor.checked.hover.background}}; }"},

    // ── ClientPuduApplet.cpp (group pudu)
    {"pudu.mode", "ClientPuduApplet.cpp: m_modeA",
     /*original*/
     "QPushButton {  background: #1a2a3a; border: 1px solid #2a4458; border-radius: 3px;  "
     "color: #8aa8c0; font-size: 11px; font-weight: bold;  padding: 3px 14px; min-width: "
     "26px;}QPushButton:hover { background: #24384e; }QPushButton:checked {  background: "
     "#3a2a0e; color: #f2c14e; border: 1px solid #f2c14e;}",
     /*themed*/
     "QPushButton {  background: {{color.button.pudu.background}}; border: 1px solid "
     "{{color.button.pudu.border}}; border-radius: 3px;  color: {{color.button.pudu.text}}; "
     "font-size: 11px; font-weight: bold;  padding: 3px 14px; min-width: "
     "26px;}QPushButton:hover { background: {{color.button.pudu.hover.background}}; "
     "}QPushButton:checked {  background: {{color.button.pudu.checked.background}}; color: "
     "{{color.button.pudu.checked.text}}; border: 1px solid "
     "{{color.button.pudu.checked.border}};}"},

    // ── ClientPuduEditor.cpp (group puduedit)
    {"puduedit.mode", "ClientPuduEditor.cpp: m_modeA",
     /*original*/
     "QPushButton {  background: #1a2a3a; border: 1px solid #2a4458; border-radius: 3px;  "
     "color: #8aa8c0; font-size: 12px; font-weight: bold;  padding: 4px 20px; min-width: "
     "34px;}QPushButton:hover { background: #24384e; }QPushButton:checked {  background: "
     "#3a2a0e; color: #f2c14e; border: 1px solid #f2c14e;}",
     /*themed*/
     "QPushButton {  background: {{color.button.puduedit.background}}; border: 1px solid "
     "{{color.button.puduedit.border}}; border-radius: 3px;  color: "
     "{{color.button.puduedit.text}}; font-size: 12px; font-weight: bold;  padding: 4px 20px; "
     "min-width: 34px;}QPushButton:hover { background: "
     "{{color.button.puduedit.hover.background}}; }QPushButton:checked {  background: "
     "{{color.button.puduedit.checked.background}}; color: "
     "{{color.button.puduedit.checked.text}}; border: 1px solid "
     "{{color.button.puduedit.checked.border}};}"},

    // ── ClientTubeEditor.cpp (group tubeeditor)
    {"tubeeditor.model", "ClientTubeEditor.cpp: btn",
     /*original*/
     "QPushButton {  background: #1a2a3a; border: 1px solid #2a4458; border-radius: 3px;  "
     "color: #8aa8c0; font-size: 11px; font-weight: bold;  padding: 3px 10px; min-width: "
     "26px;}QPushButton:hover { background: #24384e; }QPushButton:checked {  background: "
     "#3a2a0e; color: #f2c14e; border: 1px solid #f2c14e;}",
     /*themed*/
     "QPushButton {  background: {{color.button.tubeeditor.background}}; border: 1px solid "
     "{{color.button.tubeeditor.border}}; border-radius: 3px;  color: "
     "{{color.button.tubeeditor.text}}; font-size: 11px; font-weight: bold;  padding: 3px "
     "10px; min-width: 26px;}QPushButton:hover { background: "
     "{{color.button.tubeeditor.hover.background}}; }QPushButton:checked {  background: "
     "{{color.button.tubeeditor.checked.background}}; color: "
     "{{color.button.tubeeditor.checked.text}}; border: 1px solid "
     "{{color.button.tubeeditor.checked.border}};}"},

    // ── CwxPanel.cpp (group cwx)
    {"cwx.btn", "CwxPanel.cpp: m_sendBtn",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 3px; color: "
     "#c8d8e8; font-size: 11px; font-weight: bold; padding: 4px 10px; }QPushButton:checked { "
     "background: #00b4d8; color: #000; border: 1px solid #00d4f8; }QPushButton:hover { "
     "background: #203040; }",
     /*themed*/
     "QPushButton { background: {{color.button.cwx.background}}; border: 1px solid "
     "{{color.button.cwx.border}}; border-radius: 3px; color: {{color.button.cwx.text}}; "
     "font-size: 11px; font-weight: bold; padding: 4px 10px; }QPushButton:checked { "
     "background: {{color.button.cwx.checked.background}}; color: "
     "{{color.button.cwx.checked.text}}; border: 1px solid "
     "{{color.button.cwx.checked.border}}; }QPushButton:hover { background: "
     "{{color.button.cwx.hover.background}}; }"},

    {"cwx.btn.2", "CwxPanel.cpp: m_setupBtn",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 3px; color: "
     "#c8d8e8; font-size: 11px; font-weight: bold; padding: 4px 10px; }QPushButton:checked { "
     "background: #00b4d8; color: #000; border: 1px solid #00d4f8; }QPushButton:hover { "
     "background: #203040; } QPushButton { padding: 4px 6px; }",
     /*themed*/
     "QPushButton { background: {{color.button.cwx.background}}; border: 1px solid "
     "{{color.button.cwx.border}}; border-radius: 3px; color: {{color.button.cwx.text}}; "
     "font-size: 11px; font-weight: bold; padding: 4px 10px; }QPushButton:checked { "
     "background: {{color.button.cwx.checked.background}}; color: "
     "{{color.button.cwx.checked.text}}; border: 1px solid "
     "{{color.button.cwx.checked.border}}; }QPushButton:hover { background: "
     "{{color.button.cwx.hover.background}}; } QPushButton { padding: 4px 6px; }"},

    // ── DaxApplet.cpp (group dax)
    {"dax.section", "DaxApplet.cpp: this",
     /*original*/
     "QWidget { background: transparent; }QLabel { color: #8090a0; font-size: 11px; "
     "}QPushButton { background: #1a2a3a; border: 1px solid #205070;  border-radius: 3px; "
     "padding: 2px 8px; font-size: 11px; font-weight: bold; color: #c8d8e8; }QPushButton:hover "
     "{ background: #204060; }",
     /*themed*/
     "QWidget { background: transparent; }QLabel { color: #8090a0; font-size: 11px; "
     "}QPushButton { background: {{color.button.dax.background}}; border: 1px solid "
     "{{color.button.dax.border}};  border-radius: 3px; padding: 2px 8px; font-size: 11px; "
     "font-weight: bold; color: {{color.button.dax.text}}; }QPushButton:hover { background: "
     "{{color.button.dax.hover.background}}; }"},

    {"dax.green.toggle", "DaxApplet.cpp: m_daxEnable",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #205070; border-radius: 3px; color: "
     "#c8d8e8; font-size: 11px; font-weight: bold; padding: 2px 8px; }QPushButton:hover { "
     "background: #204060; }QPushButton:checked { background: #006040; color: #00ff88; border: "
     "1px solid #00a060; }",
     /*themed*/
     "QPushButton { background: {{color.button.dax.background}}; border: 1px solid "
     "{{color.button.dax.border}}; border-radius: 3px; color: {{color.button.dax.text}}; "
     "font-size: 11px; font-weight: bold; padding: 2px 8px; }QPushButton:hover { background: "
     "{{color.button.dax.hover.background}}; }QPushButton:checked { background: "
     "{{color.button.dax.checked.background}}; color: {{color.button.dax.checked.text}}; "
     "border: 1px solid {{color.button.dax.checked.border}}; }"},

    // ── DaxIqApplet.cpp (group daxiq)
    {"daxiq.section", "DaxIqApplet.cpp: this",
     /*original*/
     "QWidget { background: transparent; }QLabel { color: #8090a0; font-size: 11px; "
     "}QPushButton { background: #1a2a3a; border: 1px solid #205070;  border-radius: 3px; "
     "padding: 2px 8px; font-size: 11px; font-weight: bold; color: #c8d8e8; }QPushButton:hover "
     "{ background: #204060; }",
     /*themed*/
     "QWidget { background: transparent; }QLabel { color: #8090a0; font-size: 11px; "
     "}QPushButton { background: {{color.button.daxiq.background}}; border: 1px solid "
     "{{color.button.daxiq.border}};  border-radius: 3px; padding: 2px 8px; font-size: 11px; "
     "font-weight: bold; color: {{color.button.daxiq.text}}; }QPushButton:hover { background: "
     "{{color.button.daxiq.hover.background}}; }"},

    {"daxiq.iq.btn.off", "DaxIqApplet.cpp: m_iqEnable[i]",
     /*original*/
     "QPushButton { background: #1a2a3a; color: #8090a0; border: 1px solid #205070; padding: "
     "2px 8px; border-radius: 3px; font-size: 10px; }",
     /*themed*/
     "QPushButton { background: {{color.button.daxiq.background}}; color: "
     "{{color.button.daxiq.iq.btn.off.text}}; border: 1px solid {{color.button.daxiq.border}}; "
     "padding: 2px 8px; border-radius: 3px; font-size: 10px; }"},

    {"daxiq.iq.btn.on", "DaxIqApplet.cpp: m_iqEnable[i]",
     /*original*/
     "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; border: 1px solid "
     "#008ba8; padding: 2px 8px; border-radius: 3px; font-size: 10px; }",
     /*themed*/
     "QPushButton { background: {{color.button.daxiq.iq.btn.on.background}}; color: "
     "{{color.button.daxiq.iq.btn.on.text}}; font-weight: bold; border: 1px solid "
     "{{color.button.daxiq.iq.btn.on.border}}; padding: 2px 8px; border-radius: 3px; "
     "font-size: 10px; }"},

    // ── DspParamPopup.cpp (group dspparam)
    {"dspparam.popup", "DspParamPopup.cpp: this",
     /*original*/
     "QWidget#DspParamPopup { background: rgba(15, 15, 26, 240);  border: 1px solid #304050; "
     "border-radius: 4px; }QLabel { color: #8090a0; font-size: 11px; border: none; "
     "}QRadioButton { color: #c8d8e8; font-size: 11px; }QCheckBox { color: #c8d8e8; font-size: "
     "11px; }QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050;  "
     "border-radius: 3px; padding: 3px 8px; font-size: 11px; }QPushButton:hover { background: "
     "rgba(0, 112, 192, 180); border: 1px solid #0090e0; }",
     /*themed*/
     "QWidget#DspParamPopup { background: rgba(15, 15, 26, 240);  border: 1px solid #304050; "
     "border-radius: 4px; }QLabel { color: #8090a0; font-size: 11px; border: none; "
     "}QRadioButton { color: {{color.button.dspparam.text}}; font-size: 11px; }QCheckBox { "
     "color: {{color.button.dspparam.text}}; font-size: 11px; }QPushButton { background: "
     "{{color.button.dspparam.background}}; color: {{color.button.dspparam.text}}; border: 1px "
     "solid {{color.button.dspparam.border}};  border-radius: 3px; padding: 3px 8px; "
     "font-size: 11px; }QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px "
     "solid {{color.button.dspparam.hover.border}}; }"},

    // ── EqApplet.cpp (group eq)
    {"eq.btn.base.green.active", "EqApplet.cpp: m_onBtn",
     /*original*/
     "QPushButton { background-color: #1a2a3a; color: #c8d8e8; border: 1px solid #205070; "
     "border-radius: 3px; font-size: 11px; font-weight: bold; padding: 2px 4px; "
     "}QPushButton:checked { background-color: #006040; color: #00ff88; border: 1px solid "
     "#00a060; }",
     /*themed*/
     "QPushButton { background-color: {{color.button.eq.background}}; color: "
     "{{color.button.eq.text}}; border: 1px solid {{color.button.eq.border}}; border-radius: "
     "3px; font-size: 11px; font-weight: bold; padding: 2px 4px; }QPushButton:checked { "
     "background-color: {{color.button.eq.checked.background}}; color: "
     "{{color.button.eq.checked.text}}; border: 1px solid {{color.button.eq.checked.border}}; "
     "}"},

    {"eq.btn.base.blue.active", "EqApplet.cpp: m_rxBtn",
     /*original*/
     "QPushButton { background-color: #1a2a3a; color: #c8d8e8; border: 1px solid #205070; "
     "border-radius: 3px; font-size: 11px; font-weight: bold; padding: 2px 4px; "
     "}QPushButton:checked { background-color: #0070c0; color: #ffffff; border: 1px solid "
     "#0090e0; }",
     /*themed*/
     "QPushButton { background-color: {{color.button.eq.background}}; color: "
     "{{color.button.eq.text}}; border: 1px solid {{color.button.eq.border}}; border-radius: "
     "3px; font-size: 11px; font-weight: bold; padding: 2px 4px; }QPushButton:checked { "
     "background-color: {{color.button.eq.btn.base.blue.active.checked.background}}; color: "
     "{{color.button.eq.btn.base.blue.active.checked.text}}; border: 1px solid "
     "{{color.button.eq.btn.base.blue.active.checked.border}}; }"},

    // ── MeterApplet.cpp (group meter)
    {"meter.temp.unit", "MeterApplet.cpp: m_tempUnitBtn",
     /*original*/
     "QPushButton { background: transparent; border: 1px solid transparent; color: #8090a0; "
     "font-size: 10px; text-align: right; padding: 0 2px; }QPushButton:hover { border-color: "
     "#203040; color: #ffffff; }QPushButton:focus { border-color: #00b4d8; }",
     /*themed*/
     "QPushButton { background: transparent; border: 1px solid transparent; color: "
     "{{color.button.meter.text}}; font-size: 10px; text-align: right; padding: 0 2px; "
     "}QPushButton:hover { border-color: {{color.button.meter.hover.border}}; color: "
     "{{color.button.meter.hover.text}}; }QPushButton:focus { border-color: "
     "{{color.button.meter.focus.border}}; }"},

    // ── MqttApplet.cpp (group mqtt)
    {"mqtt.btn.off", "MqttApplet.cpp: m_enableBtn",
     /*original*/
     "QPushButton { background: #1a2a3a; color: #8090a0; border: 1px solid #205070; padding: "
     "2px 8px; border-radius: 3px; font-size: 10px; }",
     /*themed*/
     "QPushButton { background: {{color.button.mqtt.background}}; color: "
     "{{color.button.mqtt.text}}; border: 1px solid {{color.button.mqtt.border}}; padding: 2px "
     "8px; border-radius: 3px; font-size: 10px; }"},

    {"mqtt.btn.on", "MqttApplet.cpp: m_enableBtn",
     /*original*/
     "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; border: 1px solid "
     "#008ba8; padding: 2px 8px; border-radius: 3px; font-size: 10px; }",
     /*themed*/
     "QPushButton { background: {{color.button.mqtt.btn.on.background}}; color: "
     "{{color.button.mqtt.btn.on.text}}; font-weight: bold; border: 1px solid "
     "{{color.button.mqtt.btn.on.border}}; padding: 2px 8px; border-radius: 3px; font-size: "
     "10px; }"},

    {"mqtt.pub", "MqttApplet.cpp: btn",
     /*original*/
     "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #306080; padding: "
     "4px 6px; border-radius: 3px; font-size: 10px; }QPushButton:hover { background: #203850; "
     "}QPushButton:pressed { background: #00b4d8; color: #0f0f1a; }",
     /*themed*/
     "QPushButton { background: {{color.button.mqtt.background}}; color: "
     "{{color.button.mqtt.pub.text}}; border: 1px solid {{color.button.mqtt.pub.border}}; "
     "padding: 4px 6px; border-radius: 3px; font-size: 10px; }QPushButton:hover { background: "
     "{{color.button.mqtt.hover.background}}; }QPushButton:pressed { background: "
     "{{color.button.mqtt.pressed.background}}; color: {{color.button.mqtt.pressed.text}}; }"},

    // ── PhoneApplet.cpp (group phone)
    {"phone.btn.base.green.active", "PhoneApplet.cpp: m_voxBtn",
     /*original*/
     "QPushButton { background-color: #1a2a3a; color: #c8d8e8; border: 1px solid #205070; "
     "border-radius: 3px; font-size: 11px; font-weight: bold; padding: 2px 4px; "
     "}QPushButton:checked { background-color: #006040; color: #00ff88; border: 1px solid "
     "#00a060; }",
     /*themed*/
     "QPushButton { background-color: {{color.button.phone.background}}; color: "
     "{{color.button.phone.text}}; border: 1px solid {{color.button.phone.border}}; "
     "border-radius: 3px; font-size: 11px; font-weight: bold; padding: 2px 4px; "
     "}QPushButton:checked { background-color: {{color.button.phone.checked.background}}; "
     "color: {{color.button.phone.checked.text}}; border: 1px solid "
     "{{color.button.phone.checked.border}}; }"},

    {"phone.btn.base.blue.active", "PhoneApplet.cpp: m_dexpBtn",
     /*original*/
     "QPushButton { background-color: #1a2a3a; color: #c8d8e8; border: 1px solid #205070; "
     "border-radius: 3px; font-size: 11px; font-weight: bold; padding: 2px 4px; "
     "}QPushButton:checked { background-color: #0070c0; color: #ffffff; border: 1px solid "
     "#0090e0; }",
     /*themed*/
     "QPushButton { background-color: {{color.button.phone.background}}; color: "
     "{{color.button.phone.text}}; border: 1px solid {{color.button.phone.border}}; "
     "border-radius: 3px; font-size: 11px; font-weight: bold; padding: 2px 4px; "
     "}QPushButton:checked { background-color: "
     "{{color.button.phone.btn.base.blue.active.checked.background}}; color: "
     "{{color.button.phone.btn.base.blue.active.checked.text}}; border: 1px solid "
     "{{color.button.phone.btn.base.blue.active.checked.border}}; }"},

    // ── SpectrumOverlayMenu.cpp (group overlay2)
    {"overlay2.auto.rf.gain.check", "SpectrumOverlayMenu.cpp: m_autoRfGainCheck",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; color: "
     "#c8d8e8; font-size: 9px; font-weight: bold; padding: 0px; margin: 0px; "
     "}QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #0090e0; "
     "}QPushButton:hover { border: 1px solid #0090e0; }",
     /*themed*/
     "QPushButton { background: {{color.button.overlay2.background}}; border: 1px solid "
     "{{color.button.overlay2.border}}; border-radius: 2px; color: "
     "{{color.button.overlay2.text}}; font-size: 9px; font-weight: bold; padding: 0px; margin: "
     "0px; }QPushButton:checked { background: {{color.button.overlay2.checked.background}}; "
     "color: {{color.button.overlay2.checked.text}}; border: 1px solid "
     "{{color.button.overlay2.checked.border}}; }QPushButton:hover { border: 1px solid "
     "{{color.button.overlay2.hover.border}}; }"},

    // ── SpectrumWidget.cpp (group spectrum)
    {"spectrum.zoom.btn", "SpectrumWidget.cpp: btn",
     /*original*/
     "QPushButton { background: rgba(15,15,26,180); border: 1px solid #304050; border-radius: "
     "2px; color: #90a0b0; font-size: 11px; font-weight: bold; padding: 0; margin: 0; "
     "min-width: 0; }QPushButton:hover { background: rgba(30,50,70,200); color: #c8d8e8; "
     "}QPushButton:checked { background: rgba(0,180,216,210); color: #000; "
     "}QPushButton:pressed { background: #00b4d8; color: #000; }QPushButton:disabled { "
     "background: {{color.spectrum.zoomButton.disabled.background}}; border-color: "
     "{{color.spectrum.zoomButton.disabled.border}}; color: "
     "{{color.spectrum.zoomButton.disabled.text}}; }",
     /*themed*/
     "QPushButton { background: rgba(15,15,26,180); border: 1px solid "
     "{{color.button.spectrum.border}}; border-radius: 2px; color: "
     "{{color.button.spectrum.text}}; font-size: 11px; font-weight: bold; padding: 0; margin: "
     "0; min-width: 0; }QPushButton:hover { background: rgba(30,50,70,200); color: "
     "{{color.button.spectrum.hover.text}}; }QPushButton:checked { background: "
     "rgba(0,180,216,210); color: {{color.button.spectrum.checked.text}}; }QPushButton:pressed "
     "{ background: {{color.button.spectrum.pressed.background}}; color: "
     "{{color.button.spectrum.pressed.text}}; }QPushButton:disabled { background: "
     "{{color.spectrum.zoomButton.disabled.background}}; border-color: "
     "{{color.spectrum.zoomButton.disabled.border}}; color: "
     "{{color.spectrum.zoomButton.disabled.text}}; }"},

    {"spectrum.rate.btn", "SpectrumWidget.cpp: btn",
     /*original*/
     "QPushButton { background: rgba(15,15,26,90); border: 1px solid rgba(144,160,176,110); "
     "border-radius: 2px; color: rgba(200,216,232,150); font-size: 13px; font-weight: bold; "
     "padding: 0; margin: 0; min-width: 0; }QPushButton:hover { background: "
     "rgba(30,50,70,150); color: rgba(220,232,244,220); }QPushButton:pressed { background: "
     "rgba(0,180,216,150); color: #000; }",
     /*themed*/
     "QPushButton { background: rgba(15,15,26,90); border: 1px solid rgba(144,160,176,110); "
     "border-radius: 2px; color: rgba(200,216,232,150); font-size: 13px; font-weight: bold; "
     "padding: 0; margin: 0; min-width: 0; }QPushButton:hover { background: "
     "rgba(30,50,70,150); color: rgba(220,232,244,220); }QPushButton:pressed { background: "
     "rgba(0,180,216,150); color: {{color.button.spectrum.pressed.text}}; }"},

    // ── TciApplet.cpp (group tci)
    {"tci.section", "TciApplet.cpp: this",
     /*original*/
     "QWidget { background: transparent; }QLabel { color: #8090a0; font-size: 11px; "
     "}QPushButton { background: #1a2a3a; border: 1px solid #205070;  border-radius: 3px; "
     "padding: 2px 8px; font-size: 11px; font-weight: bold; color: #c8d8e8; }QPushButton:hover "
     "{ background: #204060; }",
     /*themed*/
     "QWidget { background: transparent; }QLabel { color: #8090a0; font-size: 11px; "
     "}QPushButton { background: {{color.button.tci.background}}; border: 1px solid "
     "{{color.button.tci.border}};  border-radius: 3px; padding: 2px 8px; font-size: 11px; "
     "font-weight: bold; color: {{color.button.tci.text}}; }QPushButton:hover { background: "
     "{{color.button.tci.hover.background}}; }"},

    {"tci.center.lock.iq", "TciApplet.cpp: m_centerLockIq",
     /*original*/
     "QCheckBox { color: #8090a0; font-size: 11px; }",
     /*themed*/
     "QCheckBox { color: {{color.button.tci.center.lock.iq.text}}; font-size: 11px; }"},

    {"tci.green.toggle", "TciApplet.cpp: m_tciEnable",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #205070; border-radius: 3px; color: "
     "#c8d8e8; font-size: 11px; font-weight: bold; padding: 2px 8px; }QPushButton:hover { "
     "background: #204060; }QPushButton:checked { background: #006040; color: #00ff88; border: "
     "1px solid #00a060; }",
     /*themed*/
     "QPushButton { background: {{color.button.tci.background}}; border: 1px solid "
     "{{color.button.tci.border}}; border-radius: 3px; color: {{color.button.tci.text}}; "
     "font-size: 11px; font-weight: bold; padding: 2px 8px; }QPushButton:hover { background: "
     "{{color.button.tci.hover.background}}; }QPushButton:checked { background: "
     "{{color.button.tci.checked.background}}; color: {{color.button.tci.checked.text}}; "
     "border: 1px solid {{color.button.tci.checked.border}}; }"},

    // ── TitleBar.cpp (group titlebar)
    {"titlebar.btn", "TitleBar.cpp: btn",
     /*original*/
     "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 3px; color: "
     "#c8d8e8; font-size: 12px; font-weight: bold; padding: 6px 12px; }QPushButton:hover { "
     "background: #203040; }",
     /*themed*/
     "QPushButton { background: {{color.button.titlebar.background}}; border: 1px solid "
     "{{color.button.titlebar.border}}; border-radius: 3px; color: "
     "{{color.button.titlebar.text}}; font-size: 12px; font-weight: bold; padding: 6px 12px; "
     "}QPushButton:hover { background: {{color.button.titlebar.hover.background}}; }"},

    {"titlebar.bug", "TitleBar.cpp: bugBtn",
     /*original*/
     "QPushButton { background: #cc4040; color: {{color.text.primary}}; font-weight: bold; "
     "border-radius: 4px; padding: 8px 20px; font-size: 13px; }QPushButton:hover { background: "
     "#dd5050; }",
     /*themed*/
     "QPushButton { background: {{color.button.titlebar.bug.background}}; color: "
     "{{color.text.primary}}; font-weight: bold; border-radius: 4px; padding: 8px 20px; "
     "font-size: 13px; }QPushButton:hover { background: "
     "{{color.button.titlebar.bug.hover.background}}; }"},

    // ── VfoWidget.cpp (group vfo2)
    {"vfo2.c", "VfoWidget.cpp: c",
     /*original*/
     "QCheckBox { background: transparent; color: #c8d8e8; font-size: 12px; spacing: 5px; "
     "}QCheckBox::indicator { width: 13px; height: 13px; border-radius: 2px; border: 1px solid "
     "#304050; background: #1a2a3a; }QCheckBox::indicator:checked { background: #0070c0; "
     "border: 1px solid #0090e0; }QCheckBox:disabled { color: #5a6a78; "
     "}QCheckBox::indicator:disabled { border: 1px solid #243240; background: #141f2a; }",
     /*themed*/
     "QCheckBox { background: transparent; color: {{color.button.vfo2.text}}; font-size: 12px; "
     "spacing: 5px; }QCheckBox::indicator { width: 13px; height: 13px; border-radius: 2px; "
     "border: 1px solid {{color.button.vfo2.indicator.border}}; background: "
     "{{color.button.vfo2.indicator.background}}; }QCheckBox::indicator:checked { background: "
     "{{color.button.vfo2.indicator.checked.background}}; border: 1px solid "
     "{{color.button.vfo2.indicator.checked.border}}; }QCheckBox:disabled { color: "
     "{{color.button.vfo2.disabled.text}}; }QCheckBox::indicator:disabled { border: 1px solid "
     "{{color.button.vfo2.indicator.disabled.border}}; background: "
     "{{color.button.vfo2.indicator.disabled.background}}; }"},

    // ── ContainerTitleBar.cpp (group container)
    {"container.float.btn", "ContainerTitleBar.cpp: m_floatBtn",
     /*original*/
     "QPushButton { background: transparent; border: none; color: #c8d8e8; font-size: 15px; "
     "font-weight: bold; padding: 0px 4px; } QPushButton:hover { color: #ffffff; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; color: "
     "{{color.button.container.text}}; font-size: 15px; font-weight: bold; padding: 0px 4px; } "
     "QPushButton:hover { color: {{color.button.container.hover.text}}; }"},

    {"container.btn", "ContainerTitleBar.cpp: m_closeBtn",
     /*original*/
     "QPushButton { background: transparent; border: none; color: #c8d8e8; font-size: 11px; "
     "font-weight: bold; padding: 0px 4px; } QPushButton:hover { color: #ffffff; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; color: "
     "{{color.button.container.text}}; font-size: 11px; font-weight: bold; padding: 0px 4px; } "
     "QPushButton:hover { color: {{color.button.container.hover.text}}; }"},

    // ── AtuPreTuneDialog.cpp (group atupretune)
    {"atupretune.start", "AtuPreTuneDialog.cpp: m_startBtn",
     /*original*/
     "QPushButton { background: #006030; border: 1px solid #008040; color: "
     "{{color.text.primary}}; padding: 4px 12px; font-weight: bold; }QPushButton:hover { "
     "background: #007038; }QPushButton:disabled { background: #1a2a1a; color: #556070; }",
     /*themed*/
     "QPushButton { background: {{color.button.atupretune.background}}; border: 1px solid "
     "{{color.button.atupretune.border}}; color: {{color.text.primary}}; padding: 4px 12px; "
     "font-weight: bold; }QPushButton:hover { background: "
     "{{color.button.atupretune.hover.background}}; }QPushButton:disabled { background: "
     "{{color.button.atupretune.disabled.background}}; color: "
     "{{color.button.atupretune.disabled.text}}; }"},

    {"atupretune.abort", "AtuPreTuneDialog.cpp: m_abortBtn",
     /*original*/
     "QPushButton { background: #802020; border: 1px solid {{color.accent.danger}}; color: "
     "{{color.text.primary}}; padding: 4px 12px; font-weight: bold; }QPushButton:hover { "
     "background: #903030; }",
     /*themed*/
     "QPushButton { background: {{color.button.atupretune.abort.background}}; border: 1px "
     "solid {{color.accent.danger}}; color: {{color.text.primary}}; padding: 4px 12px; "
     "font-weight: bold; }QPushButton:hover { background: "
     "{{color.button.atupretune.abort.hover.background}}; }"},

    // ── AudioDeviceChangeDialog.cpp (group audiodevice)
    {"audiodevice.dialog", "AudioDeviceChangeDialog.cpp: this",
     /*original*/
     "QDialog { background: #0f0f1a; }QFrame#audioDeviceHeader {  background: "
     "qlineargradient(x1:0, y1:0, x2:1, y2:0,    stop:0 #103626, stop:1 #10283a);  "
     "border-bottom: 1px solid #00b4d8;}QLabel { color: #c8d8e8; background: transparent; "
     "}QLabel#eyebrow { color: #40ff80; background: transparent; font-weight: bold; "
     "}QLabel#title { color: #ffffff; background: transparent; font-size: 18px; font-weight: "
     "bold; }QLabel#body { color: #c8d8e8; background: transparent; }QLabel#sectionLabel { "
     "color: #00b4d8; background: transparent; font-weight: bold; }QCheckBox { color: #c8d8e8; "
     "spacing: 8px; }QCheckBox::indicator { width: 16px; height: 16px;  border: 1px solid "
     "#304050; border-radius: 3px; background: #0a0a14; }QCheckBox::indicator:hover { "
     "border-color: #00b4d8; }QCheckBox::indicator:checked { background: #00b4d8; "
     "border-color: #00c8f0; }QListWidget {  background: #0a0a14;  border: 1px solid #304050;  "
     "border-radius: 3px;  color: #c8d8e8;  alternate-background-color: #101826;  outline: "
     "none;}QListWidget::item { padding: 6px 8px; border-bottom: 1px solid #182838; "
     "}QListWidget::item:selected { background: #12384a; color: #ffffff; "
     "}QListWidget::item:hover { background: #203040; }QPushButton {  background: #1a2a3a;  "
     "border: 1px solid #304050;  border-radius: 3px;  color: #c8d8e8;  padding: 6px "
     "14px;}QPushButton:hover { background: #203040; border-color: #00b4d8; "
     "}QPushButton:pressed { background: #102030; }QPushButton#primaryButton {  background: "
     "#00b4d8;  border-color: #00c8f0;  color: #0f0f1a;  font-weight: "
     "bold;}QPushButton#primaryButton:hover { background: #00c8f0; }QPushButton#cancelButton { "
     "color: #8aa8c0; }QScrollBar:vertical { background: #0a0a14; width: 8px; "
     "}QScrollBar::handle:vertical { background: #304050; border-radius: 4px; "
     "}QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }",
     /*themed*/
     "QDialog { background: #0f0f1a; }QFrame#audioDeviceHeader {  background: "
     "qlineargradient(x1:0, y1:0, x2:1, y2:0,    stop:0 #103626, stop:1 #10283a);  "
     "border-bottom: 1px solid #00b4d8;}QLabel { color: #c8d8e8; background: transparent; "
     "}QLabel#eyebrow { color: #40ff80; background: transparent; font-weight: bold; "
     "}QLabel#title { color: #ffffff; background: transparent; font-size: 18px; font-weight: "
     "bold; }QLabel#body { color: #c8d8e8; background: transparent; }QLabel#sectionLabel { "
     "color: #00b4d8; background: transparent; font-weight: bold; }QCheckBox { color: "
     "{{color.button.audiodevice.text}}; spacing: 8px; }QCheckBox::indicator { width: 16px; "
     "height: 16px;  border: 1px solid {{color.button.audiodevice.indicator.border}}; "
     "border-radius: 3px; background: {{color.button.audiodevice.indicator.background}}; "
     "}QCheckBox::indicator:hover { border-color: "
     "{{color.button.audiodevice.indicator.hover.border}}; }QCheckBox::indicator:checked { "
     "background: {{color.button.audiodevice.indicator.checked.background}}; border-color: "
     "{{color.button.audiodevice.indicator.checked.border}}; }QListWidget {  background: "
     "#0a0a14;  border: 1px solid #304050;  border-radius: 3px;  color: #c8d8e8;  "
     "alternate-background-color: #101826;  outline: none;}QListWidget::item { padding: 6px "
     "8px; border-bottom: 1px solid #182838; }QListWidget::item:selected { background: "
     "#12384a; color: #ffffff; }QListWidget::item:hover { background: #203040; }QPushButton {  "
     "background: {{color.button.audiodevice.background}};  border: 1px solid "
     "{{color.button.audiodevice.border}};  border-radius: 3px;  color: "
     "{{color.button.audiodevice.text}};  padding: 6px 14px;}QPushButton:hover { background: "
     "{{color.button.audiodevice.hover.background}}; border-color: "
     "{{color.button.audiodevice.hover.border}}; }QPushButton:pressed { background: "
     "{{color.button.audiodevice.pressed.background}}; }QPushButton#primaryButton {  "
     "background: {{color.button.audiodevice.primarybutton.background}};  border-color: "
     "{{color.button.audiodevice.primarybutton.border}};  color: "
     "{{color.button.audiodevice.primarybutton.text}};  font-weight: "
     "bold;}QPushButton#primaryButton:hover { background: "
     "{{color.button.audiodevice.primarybutton.hover.background}}; }QPushButton#cancelButton { "
     "color: {{color.button.audiodevice.cancelbutton.text}}; }QScrollBar:vertical { "
     "background: #0a0a14; width: 8px; }QScrollBar::handle:vertical { background: #304050; "
     "border-radius: 4px; }QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
     "height: 0; }"},

    // ── ClientDisconnectDialog.cpp (group disconnect)
    {"disconnect.dialog", "ClientDisconnectDialog.cpp: this",
     /*original*/
     "QDialog { background: #0f0f1a; }QFrame#clientDisconnectHeader {  background: "
     "qlineargradient(x1:0, y1:0, x2:1, y2:0,    stop:0 #103626, stop:1 #10283a);  "
     "border-bottom: 1px solid #00b4d8;}QLabel { color: #c8d8e8; background: transparent; "
     "}QLabel#eyebrow { color: #40ff80; background: transparent; font-weight: bold; "
     "}QLabel#title { color: #ffffff; background: transparent; font-size: 18px; font-weight: "
     "bold; }QLabel#body { color: #c8d8e8; background: transparent; }QLabel#prompt { color: "
     "#8aa8c0; background: transparent; font-weight: bold; }QPushButton {  background: "
     "#1a2a3a;  border: 1px solid #304050;  border-radius: 3px;  color: #c8d8e8;  padding: 6px "
     "14px;}QPushButton:hover { background: #203040; border-color: #00b4d8; "
     "}QPushButton:pressed { background: #102030; }QPushButton#primaryButton {  background: "
     "#00b4d8;  border-color: #00c8f0;  color: #0f0f1a;  font-weight: "
     "bold;}QPushButton#primaryButton:hover { background: #00c8f0; }QPushButton#cancelButton { "
     "color: #8aa8c0; }QScrollArea { background: transparent; border: none; "
     "}QScrollBar:vertical { background: #0a0a14; width: 8px; }QScrollBar::handle:vertical { "
     "background: #304050; border-radius: 4px; }QScrollBar::add-line:vertical, "
     "QScrollBar::sub-line:vertical { height: 0; }",
     /*themed*/
     "QDialog { background: #0f0f1a; }QFrame#clientDisconnectHeader {  background: "
     "qlineargradient(x1:0, y1:0, x2:1, y2:0,    stop:0 #103626, stop:1 #10283a);  "
     "border-bottom: 1px solid #00b4d8;}QLabel { color: #c8d8e8; background: transparent; "
     "}QLabel#eyebrow { color: #40ff80; background: transparent; font-weight: bold; "
     "}QLabel#title { color: #ffffff; background: transparent; font-size: 18px; font-weight: "
     "bold; }QLabel#body { color: #c8d8e8; background: transparent; }QLabel#prompt { color: "
     "#8aa8c0; background: transparent; font-weight: bold; }QPushButton {  background: "
     "{{color.button.disconnect.background}};  border: 1px solid "
     "{{color.button.disconnect.border}};  border-radius: 3px;  color: "
     "{{color.button.disconnect.text}};  padding: 6px 14px;}QPushButton:hover { background: "
     "{{color.button.disconnect.hover.background}}; border-color: "
     "{{color.button.disconnect.hover.border}}; }QPushButton:pressed { background: "
     "{{color.button.disconnect.pressed.background}}; }QPushButton#primaryButton {  "
     "background: {{color.button.disconnect.primarybutton.background}};  border-color: "
     "{{color.button.disconnect.primarybutton.border}};  color: "
     "{{color.button.disconnect.primarybutton.text}};  font-weight: "
     "bold;}QPushButton#primaryButton:hover { background: "
     "{{color.button.disconnect.primarybutton.hover.background}}; }QPushButton#cancelButton { "
     "color: {{color.button.disconnect.cancelbutton.text}}; }QScrollArea { background: "
     "transparent; border: none; }QScrollBar:vertical { background: #0a0a14; width: 8px; "
     "}QScrollBar::handle:vertical { background: #304050; border-radius: 4px; "
     "}QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"},

    // ── ConnectedStationsDialog.cpp (group stations)
    {"stations.style", "ConnectedStationsDialog.cpp: this",
     /*original*/
     "QDialog { background: #0f0f1a; }QFrame#radioSection {  background: #121220;  "
     "border-bottom: 1px solid #1e2840;}QFrame#stationsSection {  background: "
     "#0f0f1a;}QLabel#sectionHeader {  color: #ffffff;  font-size: 13px;  font-weight: bold;  "
     "background: transparent;}QLabel#radioModel {  color: #e0eaf8;  font-size: 14px;  "
     "font-weight: bold;  background: transparent;}QLabel#radioStation {  color: #00b4d8;  "
     "font-size: 12px;  background: transparent;}QLabel#infoLabel {  color: #8aa8c0;  "
     "font-size: 11px;  background: transparent;}QFrame#stationRow {  background: #171728;  "
     "border: 1px solid #1e2840;  border-radius: 3px;}QFrame#stationRow:hover {  background: "
     "#1a2035;  border-color: #304060;}QRadioButton {  color: #c8d8e8;  background: "
     "transparent;  spacing: 8px;}QRadioButton::indicator { width: 14px; height: 14px; "
     "}QRadioButton::indicator:checked { color: #00b4d8; }QPushButton {  background: #1a2a3a;  "
     "border: 1px solid #304050;  border-radius: 3px;  color: #c8d8e8;  padding: 6px "
     "16px;}QPushButton:hover { background: #203040; border-color: #00b4d8; "
     "}QPushButton:pressed { background: #102030; }QPushButton#disconnectButton {  background: "
     "#c0392b;  border-color: #e74c3c;  color: #ffffff;  font-weight: "
     "bold;}QPushButton#disconnectButton:hover { background: #e74c3c; "
     "}QPushButton#disconnectButton:disabled {  background: #2a1a1a;  border-color: #3a2a2a;  "
     "color: #604040;}QPushButton#cancelButton { color: #8aa8c0; }",
     /*themed*/
     "QDialog { background: #0f0f1a; }QFrame#radioSection {  background: #121220;  "
     "border-bottom: 1px solid #1e2840;}QFrame#stationsSection {  background: "
     "#0f0f1a;}QLabel#sectionHeader {  color: #ffffff;  font-size: 13px;  font-weight: bold;  "
     "background: transparent;}QLabel#radioModel {  color: #e0eaf8;  font-size: 14px;  "
     "font-weight: bold;  background: transparent;}QLabel#radioStation {  color: #00b4d8;  "
     "font-size: 12px;  background: transparent;}QLabel#infoLabel {  color: #8aa8c0;  "
     "font-size: 11px;  background: transparent;}QFrame#stationRow {  background: #171728;  "
     "border: 1px solid #1e2840;  border-radius: 3px;}QFrame#stationRow:hover {  background: "
     "#1a2035;  border-color: #304060;}QRadioButton {  color: {{color.button.stations.text}};  "
     "background: transparent;  spacing: 8px;}QRadioButton::indicator { width: 14px; height: "
     "14px; }QRadioButton::indicator:checked { color: "
     "{{color.button.stations.indicator.checked.text}}; }QPushButton {  background: "
     "{{color.button.stations.background}};  border: 1px solid "
     "{{color.button.stations.border}};  border-radius: 3px;  color: "
     "{{color.button.stations.text}};  padding: 6px 16px;}QPushButton:hover { background: "
     "{{color.button.stations.hover.background}}; border-color: "
     "{{color.button.stations.hover.border}}; }QPushButton:pressed { background: "
     "{{color.button.stations.pressed.background}}; }QPushButton#disconnectButton {  "
     "background: {{color.button.stations.disconnectbutton.background}};  border-color: "
     "{{color.button.stations.disconnectbutton.border}};  color: "
     "{{color.button.stations.disconnectbutton.text}};  font-weight: "
     "bold;}QPushButton#disconnectButton:hover { background: "
     "{{color.button.stations.disconnectbutton.hover.background}}; "
     "}QPushButton#disconnectButton:disabled {  background: "
     "{{color.button.stations.disconnectbutton.disabled.background}};  border-color: "
     "{{color.button.stations.disconnectbutton.disabled.border}};  color: "
     "{{color.button.stations.disconnectbutton.disabled.text}};}QPushButton#cancelButton { "
     "color: {{color.button.stations.cancelbutton.text}}; }"},

    // ── DxClusterStartupCommandsDialog.cpp (group dxcommands)
    {"dxcommands.ok", "DxClusterStartupCommandsDialog.cpp: okBtn",
     /*original*/
     "QPushButton { background: {{color.accent}}; color: {{color.background.0}}; font-weight: "
     "bold; border: 1px solid {{color.accent.dim}}; border-radius: 3px; padding: 6px 16px; "
     "font-size: 11px; }QPushButton:hover { background: {{color.accent.bright}}; "
     "}QPushButton:default { border: 2px solid #00f0ff; }",
     /*themed*/
     "QPushButton { background: {{color.accent}}; color: {{color.background.0}}; font-weight: "
     "bold; border: 1px solid {{color.accent.dim}}; border-radius: 3px; padding: 6px 16px; "
     "font-size: 11px; }QPushButton:hover { background: {{color.accent.bright}}; "
     "}QPushButton:default { border: 2px solid {{color.button.dxcommands.default.border}}; }"},

    // ── HelpDialog.cpp (group help)
    {"help.find.button", "HelpDialog.cpp: m_findPrevButton",
     /*original*/
     "QPushButton {  background: #203040;  color: #d8e4ee;  border: 1px solid #38506a;  "
     "border-radius: 4px;  min-width: 72px;  min-height: 28px;  padding: 0 "
     "12px;}QPushButton:hover:enabled { background: #28435a; }QPushButton:disabled { color: "
     "#5f7080; border-color: #263442; }",
     /*themed*/
     "QPushButton {  background: {{color.button.help.background}};  color: "
     "{{color.button.help.text}};  border: 1px solid {{color.button.help.border}};  "
     "border-radius: 4px;  min-width: 72px;  min-height: 28px;  padding: 0 "
     "12px;}QPushButton:hover:enabled { background: "
     "{{color.button.help.hover.enabled.background}}; }QPushButton:disabled { color: "
     "{{color.button.help.disabled.text}}; border-color: "
     "{{color.button.help.disabled.border}}; }"},

    {"help.close", "HelpDialog.cpp: closeButton",
     /*original*/
     "QPushButton {  background: {{color.accent}};  color: {{color.background.0}};  "
     "font-weight: 700;  border: none;  border-radius: 16px;  min-width: 96px;  min-height: "
     "32px;  padding: 0 18px;}QPushButton:hover { background: #18c8ea; }",
     /*themed*/
     "QPushButton {  background: {{color.accent}};  color: {{color.background.0}};  "
     "font-weight: 700;  border: none;  border-radius: 16px;  min-width: 96px;  min-height: "
     "32px;  padding: 0 18px;}QPushButton:hover { background: "
     "{{color.button.help.hover.background}}; }"},

    // ── MidiMappingDialog.cpp (group midi)
    {"midi.btn", "MidiMappingDialog.cpp: btn",
     /*original*/
     "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; border: 1px solid "
     "#008ba8; padding: 5px 14px; border-radius: 3px; }QPushButton:hover { background: "
     "#00c8f0; }QPushButton:disabled { background: #404060; color: #808080; }",
     /*themed*/
     "QPushButton { background: {{color.button.midi.background}}; color: "
     "{{color.button.midi.text}}; font-weight: bold; border: 1px solid "
     "{{color.button.midi.border}}; padding: 5px 14px; border-radius: 3px; }QPushButton:hover "
     "{ background: {{color.button.midi.hover.background}}; }QPushButton:disabled { "
     "background: {{color.button.midi.disabled.background}}; color: "
     "{{color.button.midi.disabled.text}}; }"},

    {"midi.del", "MidiMappingDialog.cpp: delBtn",
     /*original*/
     "QPushButton { background: #602020; color: #ffb0b0; font-size: 11px; font-weight: bold; "
     "padding: 0; border: 1px solid #803030; border-radius: 3px; }QPushButton:hover { "
     "background: #803030; color: #ffffff; }",
     /*themed*/
     "QPushButton { background: {{color.button.midi.del.background}}; color: "
     "{{color.button.midi.del.text}}; font-size: 11px; font-weight: bold; padding: 0; border: "
     "1px solid {{color.button.midi.del.border}}; border-radius: 3px; }QPushButton:hover { "
     "background: {{color.button.midi.del.hover.background}}; color: "
     "{{color.button.midi.hover.text}}; }"},

    // ── MqttSettingsDialog.cpp (group mqttsettings)
    {"mqttsettings.cb", "MqttSettingsDialog.cpp: cb",
     /*original*/
     "QCheckBox:disabled { color: #556; }",
     /*themed*/
     "QCheckBox:disabled { color: {{color.button.mqttsettings.disabled.text}}; }"},

    // ── ProfileImportExportDialog.cpp (group profileio)
    {"profileio.dialog", "ProfileImportExportDialog.cpp: this",
     /*original*/
     "QDialog { background: #0f0f1a; color: #c8d8e8; }QTabWidget::pane { border: 1px solid "
     "#203040; background: #0f0f1a; }QTabBar::tab { background: #1a2a3a; color: #8898a8; "
     "padding: 6px 14px;  border: 1px solid #203040; border-bottom: none; "
     "border-top-left-radius: 4px;  border-top-right-radius: 4px; margin-right: 2px; "
     "}QTabBar::tab:selected { background: #0f0f1a; color: #c8d8e8; }QGroupBox { border: 1px "
     "solid #203040; border-radius: 4px; margin-top: 10px;  padding: 10px 8px 8px 8px; color: "
     "#c8d8e8; }QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; "
     "}QLineEdit { background: #0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  "
     "padding: 4px 6px; color: #c8d8e8; }QPushButton { background: #1a2a3a; border: 1px solid "
     "#203040;  border-radius: 3px; padding: 4px 12px; color: #c8d8e8; }QPushButton:hover { "
     "background: #2a3a4a; }QPushButton:disabled { color: #607080; background: #101824; "
     "}QCheckBox { color: #c8d8e8; spacing: 8px; }QCheckBox::indicator { width: 16px; height: "
     "16px;  border: 1px solid #406080; border-radius: 3px; background: #0a0a18; "
     "}QCheckBox::indicator:checked { background: #00b4d8; }QProgressBar { background: "
     "#0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  color: #c8d8e8; text-align: "
     "center; }QProgressBar::chunk { background: #00b4d8; }",
     /*themed*/
     "QDialog { background: #0f0f1a; color: #c8d8e8; }QTabWidget::pane { border: 1px solid "
     "#203040; background: #0f0f1a; }QTabBar::tab { background: #1a2a3a; color: #8898a8; "
     "padding: 6px 14px;  border: 1px solid #203040; border-bottom: none; "
     "border-top-left-radius: 4px;  border-top-right-radius: 4px; margin-right: 2px; "
     "}QTabBar::tab:selected { background: #0f0f1a; color: #c8d8e8; }QGroupBox { border: 1px "
     "solid #203040; border-radius: 4px; margin-top: 10px;  padding: 10px 8px 8px 8px; color: "
     "#c8d8e8; }QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; "
     "}QLineEdit { background: #0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  "
     "padding: 4px 6px; color: #c8d8e8; }QPushButton { background: "
     "{{color.button.profileio.background}}; border: 1px solid "
     "{{color.button.profileio.border}};  border-radius: 3px; padding: 4px 12px; color: "
     "{{color.button.profileio.text}}; }QPushButton:hover { background: "
     "{{color.button.profileio.hover.background}}; }QPushButton:disabled { color: "
     "{{color.button.profileio.disabled.text}}; background: "
     "{{color.button.profileio.disabled.background}}; }QCheckBox { color: "
     "{{color.button.profileio.text}}; spacing: 8px; }QCheckBox::indicator { width: 16px; "
     "height: 16px;  border: 1px solid {{color.button.profileio.indicator.border}}; "
     "border-radius: 3px; background: {{color.button.profileio.indicator.background}}; "
     "}QCheckBox::indicator:checked { background: "
     "{{color.button.profileio.indicator.checked.background}}; }QProgressBar { background: "
     "#0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  color: #c8d8e8; text-align: "
     "center; }QProgressBar::chunk { background: #00b4d8; }"},

    // ── ProfileManagerDialog.cpp (group profiles)
    {"profiles.dialog", "ProfileManagerDialog.cpp: this",
     /*original*/
     "QDialog { background: #0f0f1a; color: #c8d8e8; }QTabWidget::pane { border: 1px solid "
     "#203040; background: #0f0f1a; }QTabBar::tab { background: #1a2a3a; color: #8898a8; "
     "padding: 6px 14px;  border: 1px solid #203040; border-bottom: none; "
     "border-top-left-radius: 4px;  border-top-right-radius: 4px; margin-right: 2px; "
     "}QTabBar::tab:selected { background: #0f0f1a; color: #c8d8e8; }QLineEdit { background: "
     "#0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  padding: 4px 6px; color: "
     "#c8d8e8; }QListWidget { background: #0a0a18; border: 1px solid #1e2e3e; border-radius: "
     "3px;  color: #c8d8e8; }QListWidget::item:selected { background: #0070c0; }QPushButton { "
     "background: #1a2a3a; border: 1px solid #203040;  border-radius: 3px; padding: 4px 12px; "
     "color: #c8d8e8; }QPushButton:hover { background: #2a3a4a; }QPushButton:disabled { "
     "background: #141c26; border: 1px solid #1a2632;  color: #5a6a78; }QCheckBox { color: "
     "#c8d8e8; }QCheckBox::indicator { width: 16px; height: 16px;  border: 1px solid #406080; "
     "border-radius: 3px; background: #0a0a18; }QCheckBox::indicator:checked { background: "
     "#00b4d8; }",
     /*themed*/
     "QDialog { background: #0f0f1a; color: #c8d8e8; }QTabWidget::pane { border: 1px solid "
     "#203040; background: #0f0f1a; }QTabBar::tab { background: #1a2a3a; color: #8898a8; "
     "padding: 6px 14px;  border: 1px solid #203040; border-bottom: none; "
     "border-top-left-radius: 4px;  border-top-right-radius: 4px; margin-right: 2px; "
     "}QTabBar::tab:selected { background: #0f0f1a; color: #c8d8e8; }QLineEdit { background: "
     "#0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  padding: 4px 6px; color: "
     "#c8d8e8; }QListWidget { background: #0a0a18; border: 1px solid #1e2e3e; border-radius: "
     "3px;  color: #c8d8e8; }QListWidget::item:selected { background: #0070c0; }QPushButton { "
     "background: {{color.button.profiles.background}}; border: 1px solid "
     "{{color.button.profiles.border}};  border-radius: 3px; padding: 4px 12px; color: "
     "{{color.button.profiles.text}}; }QPushButton:hover { background: "
     "{{color.button.profiles.hover.background}}; }QPushButton:disabled { background: "
     "{{color.button.profiles.disabled.background}}; border: 1px solid "
     "{{color.button.profiles.disabled.border}};  color: "
     "{{color.button.profiles.disabled.text}}; }QCheckBox { color: "
     "{{color.button.profiles.text}}; }QCheckBox::indicator { width: 16px; height: 16px;  "
     "border: 1px solid {{color.button.profiles.indicator.border}}; border-radius: 3px; "
     "background: {{color.button.profiles.indicator.background}}; "
     "}QCheckBox::indicator:checked { background: "
     "{{color.button.profiles.indicator.checked.background}}; }"},

    // ── PsAmpViewDialog.cpp (group psampview)
    {"psampview.latch", "PsAmpViewDialog.cpp: b",
     /*original*/
     "QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #40b0ff; }",
     /*themed*/
     "QPushButton:checked { background: {{color.button.psampview.checked.background}}; color: "
     "{{color.button.psampview.checked.text}}; border: 1px solid "
     "{{color.button.psampview.checked.border}}; }"},

    {"psampview.two.tone", "PsAmpViewDialog.cpp: m_twoTone",
     /*original*/
     "QPushButton:checked { background: #c03030; color: #ffffff; border: 1px solid #ff5050; }",
     /*themed*/
     "QPushButton:checked { background: "
     "{{color.button.psampview.two.tone.checked.background}}; color: "
     "{{color.button.psampview.checked.text}}; border: 1px solid "
     "{{color.button.psampview.two.tone.checked.border}}; }"},

    // ── SettingsBrowserDialog.cpp (group settingsbrowser)
    {"settingsbrowser.dialog", "SettingsBrowserDialog.cpp: this",
     /*original*/
     "QDialog { background: #0f0f1a; color: #c8d8e8; }QLabel { color: #c8d8e8; }QLineEdit { "
     "background: #0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  padding: 4px 6px; "
     "color: #c8d8e8; }QTreeWidget { background: #0a0a18; border: 1px solid #1e2e3e; "
     "border-radius: 3px;  color: #c8d8e8; outline: none; }QTreeWidget::item { min-height: "
     "22px; padding: 1px 4px; }QTreeWidget::item:selected { background: #0070c0; color: "
     "#ffffff; }QTreeWidget::item:hover:!selected { background: #1a2a3a; }QTableWidget { "
     "background: #0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  color: #c8d8e8; "
     "gridline-color: #14202f;  selection-background-color: #1b3650; selection-color: #e8f4ff; "
     "}QHeaderView::section { color: #8d99ad; background: #101c2a; border: none;  "
     "border-bottom: 1px solid #233246; padding: 4px 8px; font-size: 11px; }QPlainTextEdit { "
     "background: #0a0a18; border: 1px solid #1e2e3e;  border-radius: 3px; color: #c8d8e8;  "
     "font-family: \"SF Mono\", \"Menlo\", \"Consolas\", monospace; }QPushButton { background: "
     "#1a2a3a; border: 1px solid #203040;  border-radius: 3px; padding: 4px 12px; color: "
     "#c8d8e8; }QPushButton:hover { background: #2a3a4a; }QPushButton:disabled { background: "
     "#141c26; border: 1px solid #1a2632;  color: #5a6a78; }QComboBox { background: #0a0a18; "
     "border: 1px solid #1e2e3e; border-radius: 3px;  padding: 2px 6px; color: #c8d8e8; }",
     /*themed*/
     "QDialog { background: #0f0f1a; color: #c8d8e8; }QLabel { color: #c8d8e8; }QLineEdit { "
     "background: #0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  padding: 4px 6px; "
     "color: #c8d8e8; }QTreeWidget { background: #0a0a18; border: 1px solid #1e2e3e; "
     "border-radius: 3px;  color: #c8d8e8; outline: none; }QTreeWidget::item { min-height: "
     "22px; padding: 1px 4px; }QTreeWidget::item:selected { background: #0070c0; color: "
     "#ffffff; }QTreeWidget::item:hover:!selected { background: #1a2a3a; }QTableWidget { "
     "background: #0a0a18; border: 1px solid #1e2e3e; border-radius: 3px;  color: #c8d8e8; "
     "gridline-color: #14202f;  selection-background-color: #1b3650; selection-color: #e8f4ff; "
     "}QHeaderView::section { color: #8d99ad; background: #101c2a; border: none;  "
     "border-bottom: 1px solid #233246; padding: 4px 8px; font-size: 11px; }QPlainTextEdit { "
     "background: #0a0a18; border: 1px solid #1e2e3e;  border-radius: 3px; color: #c8d8e8;  "
     "font-family: \"SF Mono\", \"Menlo\", \"Consolas\", monospace; }QPushButton { background: "
     "{{color.button.settingsbrowser.background}}; border: 1px solid "
     "{{color.button.settingsbrowser.border}};  border-radius: 3px; padding: 4px 12px; color: "
     "{{color.button.settingsbrowser.text}}; }QPushButton:hover { background: "
     "{{color.button.settingsbrowser.hover.background}}; }QPushButton:disabled { background: "
     "{{color.button.settingsbrowser.disabled.background}}; border: 1px solid "
     "{{color.button.settingsbrowser.disabled.border}};  color: "
     "{{color.button.settingsbrowser.disabled.text}}; }QComboBox { background: #0a0a18; "
     "border: 1px solid #1e2e3e; border-radius: 3px;  padding: 2px 6px; color: #c8d8e8; }"},

    // ── ShortcutDialog.cpp (group shortcuts)
    {"shortcuts.dialog", "ShortcutDialog.cpp: this",
     /*original*/
     "QDialog { background: #0f0f1a; }QLabel { color: #c8d8e8; }QPushButton { background: "
     "#1a2a3a; border: 1px solid #304050;   border-radius: 3px; color: #c8d8e8; padding: 4px "
     "12px; }QPushButton:hover { background: #203040; border-color: #00b4d8; }QComboBox { "
     "background: #1a2a3a; border: 1px solid #304050;   border-radius: 3px; color: #c8d8e8; "
     "padding: 2px 6px; }QComboBox::drop-down { border: none; }QComboBox QAbstractItemView { "
     "background: #0f0f1a; color: #c8d8e8;   selection-background-color: #1a3a5a; }QLineEdit { "
     "background: #1a2a3a; border: 1px solid #304050;   border-radius: 3px; color: #c8d8e8; "
     "padding: 2px 6px; }QTableWidget { background: #0f0f1a; color: #c8d8e8;   gridline-color: "
     "#203040; border: 1px solid #304050; }QTableWidget::item { padding: 2px 6px; "
     "}QTableWidget::item:selected { background: #1a3a5a; color: #00b4d8; "
     "}QHeaderView::section { background: #1a2a3a; color: #8aa8c0;   border: 1px solid "
     "#203040; padding: 4px; }",
     /*themed*/
     "QDialog { background: #0f0f1a; }QLabel { color: #c8d8e8; }QPushButton { background: "
     "{{color.button.shortcuts.background}}; border: 1px solid "
     "{{color.button.shortcuts.border}};   border-radius: 3px; color: "
     "{{color.button.shortcuts.text}}; padding: 4px 12px; }QPushButton:hover { background: "
     "{{color.button.shortcuts.hover.background}}; border-color: "
     "{{color.button.shortcuts.hover.border}}; }QComboBox { background: #1a2a3a; border: 1px "
     "solid #304050;   border-radius: 3px; color: #c8d8e8; padding: 2px 6px; "
     "}QComboBox::drop-down { border: none; }QComboBox QAbstractItemView { background: "
     "#0f0f1a; color: #c8d8e8;   selection-background-color: #1a3a5a; }QLineEdit { background: "
     "#1a2a3a; border: 1px solid #304050;   border-radius: 3px; color: #c8d8e8; padding: 2px "
     "6px; }QTableWidget { background: #0f0f1a; color: #c8d8e8;   gridline-color: #203040; "
     "border: 1px solid #304050; }QTableWidget::item { padding: 2px 6px; "
     "}QTableWidget::item:selected { background: #1a3a5a; color: #00b4d8; "
     "}QHeaderView::section { background: #1a2a3a; color: #8aa8c0;   border: 1px solid "
     "#203040; padding: 4px; }"},

    // ── SwrSweepLicenseDialog.cpp (group swrlicense)
    {"swrlicense.accept", "SwrSweepLicenseDialog.cpp: acceptBtn",
     /*original*/
     "QPushButton { background: {{color.accent}}; color: {{color.background.0}}; font-weight: "
     "bold; border: 1px solid {{color.accent.dim}}; border-radius: 3px; padding: 6px 16px; "
     "font-size: 11px; }QPushButton:hover { background: {{color.accent.bright}}; "
     "}QPushButton:default { border: 2px solid #00f0ff; }",
     /*themed*/
     "QPushButton { background: {{color.accent}}; color: {{color.background.0}}; font-weight: "
     "bold; border: 1px solid {{color.accent.dim}}; border-radius: 3px; padding: 6px 16px; "
     "font-size: 11px; }QPushButton:hover { background: {{color.accent.bright}}; "
     "}QPushButton:default { border: 2px solid {{color.button.swrlicense.default.border}}; }"},

    // ── AetherClockApplet.cpp (group clock, build 784)
    {"clock.base", "AetherClockApplet.cpp: drawer toggle, debug toggle (kButtonBase)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; border: 1px solid "
     "{{color.background.2}}; border-radius: 3px; color: {{color.text.primary}}; font-size: "
     "10px; font-weight: bold; padding: 2px 4px; }QPushButton:hover { background: "
     "{{color.background.2}}; }",
     /*themed*/
     "QPushButton { background: {{color.button.clock.background}}; border: 1px solid "
     "{{color.button.clock.border}}; border-radius: 3px; color: {{color.button.clock.text}}; "
     "font-size: 10px; font-weight: bold; padding: 2px 4px; }QPushButton:hover { background: "
     "{{color.button.clock.hover.background}}; }"},

    {"clock.startstop", "AetherClockApplet.cpp: Start / Stop (kButtonBase + kStartActive + kDisabledBtn)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; border: 1px solid "
     "{{color.background.2}}; border-radius: 3px; color: {{color.text.primary}}; font-size: "
     "10px; font-weight: bold; padding: 2px 4px; }QPushButton:hover { background: "
     "{{color.background.2}}; }QPushButton:checked { background-color: #006040; color: "
     "#00ff88; border: 1px solid #00a060; }QPushButton:disabled { background-color: #1a1a2a; "
     "color: #556070; border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.clock.background}}; border: 1px solid "
     "{{color.button.clock.border}}; border-radius: 3px; color: {{color.button.clock.text}}; "
     "font-size: 10px; font-weight: bold; padding: 2px 4px; }QPushButton:hover { background: "
     "{{color.button.clock.hover.background}}; }QPushButton:checked { background-color: "
     "{{color.button.clock.checked.background}}; color: {{color.button.clock.checked.text}}; "
     "border: 1px solid {{color.button.clock.checked.border}}; }QPushButton:disabled { "
     "background-color: {{color.button.clock.disabled.background}}; color: "
     "{{color.button.clock.disabled.text}}; border: 1px solid "
     "{{color.button.clock.disabled.border}}; }"},

    {"clock.tune", "AetherClockApplet.cpp: Tune (kButtonBase + kDisabledBtn)",
     /*original*/
     "QPushButton { background: {{color.background.1}}; border: 1px solid "
     "{{color.background.2}}; border-radius: 3px; color: {{color.text.primary}}; font-size: "
     "10px; font-weight: bold; padding: 2px 4px; }QPushButton:hover { background: "
     "{{color.background.2}}; }QPushButton:disabled { background-color: #1a1a2a; color: "
     "#556070; border: 1px solid #2a3040; }",
     /*themed*/
     "QPushButton { background: {{color.button.clock.background}}; border: 1px solid "
     "{{color.button.clock.border}}; border-radius: 3px; color: {{color.button.clock.text}}; "
     "font-size: 10px; font-weight: bold; padding: 2px 4px; }QPushButton:hover { background: "
     "{{color.button.clock.hover.background}}; }QPushButton:disabled { background-color: "
     "{{color.button.clock.disabled.background}}; color: {{color.button.clock.disabled.text}}; "
     "border: 1px solid {{color.button.clock.disabled.border}}; }"},

    // ── stripout (build 784)
    {"stripout.quin.on", "StripFinalOutputPanel.cpp: QUIN while intro / outro plays",
     /*original*/
     "QPushButton {  background: {{color.accent.danger}}; color: #ffffff;  border: 1px solid "
     "{{color.accent.danger}};  border-radius: 3px; font-size: 10px;  font-weight: bold; "
     "padding: 1px;}",
     /*themed*/
     "QPushButton {  background: {{color.accent.danger}}; color: "
     "{{color.button.stripout.text}};  border: 1px solid {{color.accent.danger}};  "
     "border-radius: 3px; font-size: 10px;  font-weight: bold; padding: 1px;}"},

    // ── whatsnew (build 784)
    {"whatsnew.secondary", "WhatsNewDialog.cpp: Find / Upgrade / Skip (secondaryButtonStyle)",
     /*original*/
     "QPushButton { background: #1a2a3a; color: #d6e2ee; font-weight: bold; font-size: 13px; "
     "border-radius: 6px; padding: 0 24px; border: 1px solid #304050; }QPushButton:hover { "
     "background: #20384c; }QPushButton:pressed { background: #162838; }",
     /*themed*/
     "QPushButton { background: {{color.button.whatsnew.background}}; color: "
     "{{color.button.whatsnew.text}}; font-weight: bold; font-size: 13px; border-radius: 6px; "
     "padding: 0 24px; border: 1px solid {{color.button.whatsnew.border}}; }QPushButton:hover "
     "{ background: {{color.button.whatsnew.hover.background}}; }QPushButton:pressed { "
     "background: {{color.button.whatsnew.pressed.background}}; }"},

    {"whatsnew.primary", "WhatsNewDialog.cpp: Close (primaryButtonStyle)",
     /*original*/
     "QPushButton { background: #00b4d8; color: #071018; font-weight: bold; font-size: 13px; "
     "border-radius: 6px; padding: 0 28px; border: none; }QPushButton:hover { background: "
     "#17c9ea; }QPushButton:pressed { background: #0798b6; }",
     /*themed*/
     "QPushButton { background: {{color.button.whatsnew.primary.background}}; color: "
     "{{color.button.whatsnew.primary.text}}; font-weight: bold; font-size: 13px; "
     "border-radius: 6px; padding: 0 28px; border: none; }QPushButton:hover { background: "
     "{{color.button.whatsnew.primary.hover.background}}; }QPushButton:pressed { background: "
     "{{color.button.whatsnew.primary.pressed.background}}; }"},

    // ── adaptive (build 784)
    {"adaptive.check", "AdaptiveFilterControls.cpp: Adaptive RX filter check box (%1 = font size)",
     /*original*/
     "QCheckBox { background: transparent; color: #c8d8e8; font-size: %1px; spacing: 5px; "
     "}QCheckBox::indicator { width: 13px; height: 13px; border-radius: 2px; border: 1px solid "
     "#304050; background: #1a2a3a; }QCheckBox::indicator:checked { background: #0070c0; "
     "border: 1px solid #0090e0; }QCheckBox:disabled { color: #5a6a78; "
     "}QCheckBox::indicator:disabled { border: 1px solid #243240; background: #141f2a; }",
     /*themed*/
     "QCheckBox { background: transparent; color: {{color.button.adaptive.text}}; font-size: "
     "%1px; spacing: 5px; }QCheckBox::indicator { width: 13px; height: 13px; border-radius: "
     "2px; border: 1px solid {{color.button.adaptive.indicator.border}}; background: "
     "{{color.button.adaptive.indicator.background}}; }QCheckBox::indicator:checked { "
     "background: {{color.button.adaptive.indicator.checked.background}}; border: 1px solid "
     "{{color.button.adaptive.indicator.checked.border}}; }QCheckBox:disabled { color: "
     "{{color.button.adaptive.disabled.text}}; }QCheckBox::indicator:disabled { border: 1px "
     "solid {{color.button.adaptive.indicator.disabled.border}}; background: "
     "{{color.button.adaptive.indicator.disabled.background}}; }"},

    // ── frameless (build 784)
    {"frameless.button", "FramelessWindowTitleBar.cpp: minimise / maximise (%1 = font size)",
     /*original*/
     "QPushButton { background: transparent; border: none; color: #c8d8e8; font-size: %1px; "
     "font-weight: bold; padding: 0px 4px; }QPushButton:hover { color: #ffffff; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; color: "
     "{{color.button.frameless.text}}; font-size: %1px; font-weight: bold; padding: 0px 4px; "
     "}QPushButton:hover { color: {{color.button.frameless.hover.text}}; }"},

    {"frameless.close", "FramelessWindowTitleBar.cpp: close (%1 = font size)",
     /*original*/
     "QPushButton { background: transparent; border: none; color: #c8d8e8; font-size: %1px; "
     "font-weight: bold; padding: 0px 4px; }QPushButton:hover { color: #ffffff; background: "
     "#cc2030; }",
     /*themed*/
     "QPushButton { background: transparent; border: none; color: "
     "{{color.button.frameless.text}}; font-size: %1px; font-weight: bold; padding: 0px 4px; "
     "}QPushButton:hover { color: {{color.button.frameless.hover.text}}; background: "
     "{{color.button.frameless.hover.background}}; }"},

    // ── swrlicense (build 784)
    {"swrlicense.remember", "SwrSweepLicenseDialog.cpp: Remember my answer",
     /*original*/
     "QCheckBox { color: {{color.text.secondary}}; font-size: 11px; }QCheckBox::indicator { "
     "width: 14px; height: 14px; border: 1px solid #406080; border-radius: 2px; background: "
     "{{color.background.0}}; }QCheckBox::indicator:checked { background: {{color.accent}}; }",
     /*themed*/
     "QCheckBox { color: {{color.text.secondary}}; font-size: 11px; }QCheckBox::indicator { "
     "width: 14px; height: 14px; border: 1px solid "
     "{{color.button.swrlicense.indicator.border}}; border-radius: 2px; background: "
     "{{color.background.0}}; }QCheckBox::indicator:checked { background: {{color.accent}}; }"},
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
