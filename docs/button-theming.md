# Button theming (ANAN fork, from build 770)

Goal (operator, 26.11): every button of the program can be restyled from the theme editor —
by group, by place, by likeness of style — so users can build high-quality themes. **First
priority: the current look does not change.**

## How a button is converted

1. Its style sheet moves to `src/gui/ButtonStyles.cpp` as one catalogue entry:
   - `original` — the sheet exactly as it stood in the code;
   - `themed` — the same sheet, its literal colours replaced by `{{color.button.<group>.<part>}}`.
2. The code calls `ButtonStyles::apply(button, "<id>")` (through `ThemeManager`, so the tokens
   resolve per scope and re-apply on a theme change).
3. The tokens get their values in `resources/themes/default-dark.json` under
   `color.button.<group>` — **the very colours of the original sheet**.
4. `python tools/check_button_styles.py --write` writes the remapped values into the themes with a
   literal-colour remap (Midnight, Deep Ocean) and checks every entry in every bundled theme:
   the themed sheet must resolve to exactly what the original resolved to.
5. `python tools/gen_theme_seed.py` regenerates `src/core/ThemeSeedGenerated.cpp` (the floor for
   user themes that predate the new tokens — they get the original colours).

## Restoring the old look

- **No rebuild:** set `ThemedButtons` to `False` (Settings browser or the settings file) and
  restart — every catalogued button uses its original sheet again.
- **In code:** each entry names where it is used (`site`) and holds the original text; put it back
  there, or revert the build's commit.

## Token names

`color.button.<group>.<state>.<part>`:

| part | meaning |
|---|---|
| `background`, `border`, `text` | at rest |
| `hover.background` | under the mouse |
| `checked.background / .border / .text` | switched on |
| `disabled.background / .text / .border` | not available |
| `active.background / .border / .text` | doing it now (transmitting, tuning) |
| `selected.background / .border / .text` | switched on, the blue "chosen" variant |
| `gated.background / .text` | switched off by a mode gate (RX FM offset, REV) |

Place: the theme editor's **Scope** (applet / container) overrides a group's token for one place
only. Likeness of style: planned as a family layer once the groups are in (similar groups
aliasing one family value).

## Converted

| Build | Group | Entries | Buttons |
|---|---|---|---|
| 770 | `tx` (TX applet) | `tx.action`, `tx.toggle`, `tx.mox.idle`, `tx.mox.active`, `tx.tune.active`, `tx.apd` | TUNE, MOX, ATU, MEM, MON, DUP, APD |
| 771 | `mic` (Mic applet, Phone and CW) | `mic.toggle`, `mic.select` | ACC, COMP, MON, DUP, sidetone, break-in, APF (green when on); DAX, Profile 1–4, iambic, hold delay (blue when on) |
| 772 | `rx` (RX applet) | `rx.toggle`, `rx.select`, `rx.offset`, `rx.rev`, `rx.amber`, `rx.zero`, `rx.caption`, `rx.sql.manual`, `rx.sql.auto`, `rx.preset`, `rx.lock`, `rx.ant`, `rx.txant`, `rx.qsk` | NB / NR, BIN, AGC A, FM offset − / Simplex / + and REV, RIT / XIT and their 0, NF + caption, SQL (off / manual / AUTO), filter presets, VFO lock, RX / TX antenna, QSK |

Primitive references. A group token may hold a primitive reference (`{color.gray.800}`) when the
original sheet used a generic token (`{{color.background.1}}`): the remap themes resolve it to their
own primitive, and `check_button_styles.py --write` copies such a value unchanged (build 772).

## Still to do (424 literal button rules in 56 files at build 769)

VFO flag (67), spectrum overlay menu (34), DX cluster dialog (16), shack switch
(14), client chain (13), spot settings (11), Antenna Genius (10), tuner (10), client effect
editors, PureSignal (8), container title bars (7), panadapter applet (6), DAX,
TCI, MQTT, title bars, dialogs. Dynamic sheets (built with `QString::arg`, e.g. quick memory
buttons) need tokens read in code — handled separately.
