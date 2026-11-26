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
| `hover.border` | border under the mouse (VFO flag) |
| `tab.*` | VFO flag tab labels: text, hover, focus, active, DSP on |

Place: the theme editor's **Scope** (applet / container) overrides a group's token for one place
only. Likeness of style: planned as a family layer once the groups are in (similar groups
aliasing one family value).

## Converted

| Build | Group | Entries | Buttons |
|---|---|---|---|
| 770 | `tx` (TX applet) | `tx.action`, `tx.toggle`, `tx.mox.idle`, `tx.mox.active`, `tx.tune.active`, `tx.apd` | TUNE, MOX, ATU, MEM, MON, DUP, APD |
| 771 | `mic` (Mic applet, Phone and CW) | `mic.toggle`, `mic.select` | ACC, COMP, MON, DUP, sidetone, break-in, APF (green when on); DAX, Profile 1–4, iambic, hold delay (blue when on) |
| 772 | `rx` (RX applet) | `rx.toggle`, `rx.select`, `rx.offset`, `rx.rev`, `rx.amber`, `rx.zero`, `rx.caption`, `rx.sql.manual`, `rx.sql.auto`, `rx.preset`, `rx.lock`, `rx.ant`, `rx.txant`, `rx.qsk` | NB / NR, BIN, AGC A, FM offset − / Simplex / + and REV, RIT / XIT and their 0, NF + caption, SQL (off / manual / AUTO), filter presets, VFO lock, RX / TX antenna, QSK |
| 773 | `vfo` (VFO flag, step 1: the file's style constants) | `vfo.ant.rx`, `vfo.ant.tx`, `vfo.tab`, `vfo.tab.active`, `vfo.tab.dsp`, `vfo.dsp`, `vfo.dsp.caption`, `vfo.dsp.active`, `vfo.mode`, `vfo.meter`, `vfo.sql.manual`, `vfo.sql.auto` | RX / TX antenna, tab labels (speaker, DSP, mode, X/RIT: at rest, open, DSP on), DSP toggles, DIV, ESC, AetherDSP / AetherVoice, NF caption, SQL (off / on / AUTO), quick mode buttons, filter presets, meter view selector |
| 774 | `vfo` (VFO flag, step 2) | `vfo.slice.close`, `vfo.slice.lock`, `vfo.slice.play`, `vfo.offset`, `vfo.rev`, `vfo.rit`, `vfo.zero`, `vfo.autotune` | slice close / lock / play buttons, FM offset − / Simplex / + and REV, RIT / XIT and their 0, CW autotune Once / Loop, Zero Beat |
| 775 | `vfo` (VFO flag, step 3) | `vfo.record`, `vfo.record.idle`, `vfo.record.pulse.bright`, `vfo.record.pulse.dim`, `vfo.mute`, `vfo.badge.tx`, `vfo.badge.rx`, `vfo.split.swap`, `vfo.split.on`, `vfo.split.off`, `vfo.split.lit` | slice record button (also its pulse), audio MUTE, TX badge, SPLIT / SWAP badge in all its states |
| 776 | `overlay` (spectrum overlay menu, step 1) | `overlay.menu`, `overlay.menu.active`, `overlay.toggle`, `overlay.band`, `overlay.xvtr`, `overlay.xvtr.panel`, `overlay.xvtr.blank` | menu column buttons (at rest / panel open), the arrow, band panel buttons, transverter buttons (band panel and transverter panel, HF, empty slots) |
| 777 | `overlay` (spectrum overlay menu, step 2) | `overlay.ant`, `overlay.loop`, `overlay.adc`, `overlay.check`, `overlay.frontend`, `overlay.wnb`, `overlay.wfm`, `overlay.display` | ANT panel: RX / TX antenna, LNA, LoopA / LoopB, ADC, dither / random boxes; front end PRE / ATT; WNB; WFM; Display panel buttons and Clone / Reset |

Primitive references. A group token may hold a primitive reference (`{color.gray.800}`) when the
original sheet used a generic token (`{{color.background.1}}`): the remap themes resolve it to their
own primitive, and `check_button_styles.py --write` copies such a value unchanged (build 772).

VFO flag done (build 775). Left as they are: the slice letter badge (a label in the slice's own
colour), and AGC A / DSP Reset, which already take only theme tokens (`color.canon.*`).

Translucent colours. `rgba(...)` literals stay in the sheets: a token value is `#rrggbb` or
`#aarrggbb`, and ThemeManager writes the latter as `rgba(r, g, b, 0.941)`, which is not the same text
as `rgba(r, g, b, 240)`. Only the opaque `#rrggbb` colours become tokens, so nothing moves.

Spectrum overlay menu done (build 777). Left as they are: the colour picker buttons (their colour is
the colour the operator picked) and the Display scope-mode row, whose one scoped sheet is set plainly.
Three-digit colours (`#444`, WFM) are kept as written: the remap themes do not touch them either.

## Still to do (424 literal button rules in 56 files at build 769)

DX cluster dialog (16), shack switch
(14), client chain (13), spot settings (11), Antenna Genius (10), tuner (10), client effect
editors, PureSignal (8), container title bars (7), panadapter applet (6), DAX,
TCI, MQTT, title bars, dialogs. Dynamic sheets (built with `QString::arg`, e.g. quick memory
buttons) need tokens read in code — handled separately.
