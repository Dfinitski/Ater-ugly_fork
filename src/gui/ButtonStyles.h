#pragma once

// ANAN fork build 770 (operator, 26.11: "let the theme editor change any
// buttons -- by group, by place, by likeness of size and style -- so users
// can make high-quality themes of their own; the main thing is not to break
// the current look"). The program's buttons were styled with colours
// written in the code, which the theme editor cannot reach. One by one, each
// such style moves here: the ORIGINAL style sheet, exactly as it stood in
// the code, and the THEMED one, the same sheet with its colours replaced by
// theme tokens (color.button.<group>.*) whose bundled values are those very
// colours -- so nothing changes on screen until a user edits a token.
//
// This file is the catalogue and the safety net:
//   * every converted style, with where it is used, in one place;
//   * the setting ThemedButtons = False (Settings browser, or the settings
//     file) makes every catalogued button use its ORIGINAL sheet again --
//     the look of build 769 -- without a rebuild;
//   * tools/check_button_styles.py proves, for every entry, that the themed
//     sheet with the bundled token values resolves to exactly the original
//     (Default Dark), and to the theme's own recolouring of it (Midnight,
//     Deep Ocean).

class QWidget;
class QString;

namespace AetherSDR::ButtonStyles {

// The style sheet template for `id` (themed, or original with
// ThemedButtons = False); an empty string for an unknown id.
QString sheet(const char* id);

// sheet(id) through ThemeManager::applyStyleSheet() -- tokens resolved for
// the widget's scope, re-applied on every theme change.
void apply(QWidget* widget, const char* id);

// False when the setting ThemedButtons is "False".
bool themedEnabled();

} // namespace AetherSDR::ButtonStyles
