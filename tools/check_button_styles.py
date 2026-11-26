#!/usr/bin/env python3
"""
ANAN fork build 770: proof that the themed button sheets change nothing.

src/gui/ButtonStyles.cpp keeps, per converted button style, the ORIGINAL sheet
(as it stood in the code) and the THEMED one (its colours as
color.button.<group>.* tokens). This tool resolves both the way ThemeManager
does (resolveFor: the theme's literal-colour remap on the template, then the
{{token}} values) for every bundled theme and compares them:

  * Default Dark (no remap): byte-identical.
  * Midnight / Deep Ocean (literal remap): every colour within 1 of 255 per
    channel (Qt's float -> 8-bit rounding).

  python tools/check_button_styles.py            # check, exit 1 on a mismatch
  python tools/check_button_styles.py --write    # also write the remapped
                                                 # color.button.* values into
                                                 # the remap themes' JSON

The remap themes need the remapped values because ThemeManager remaps the
literal colours of a template but not token values: with a token in place of
#1a3a5a, Midnight must carry Midnight's version of #1a3a5a in that token.
"""
from __future__ import annotations

import colorsys
import struct
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CATALOG = REPO / "src" / "gui" / "ButtonStyles.cpp"
THEMES = REPO / "resources" / "themes"
BASE = "default-dark.json"
REMAP_THEMES = ["midnight.json", "deep-ocean.json"]
SRC_NEUTRAL = ["#000000", "#0a121e", "#0f0f1a", "#1a2330", "#1a2a3a", "#304050",
               "#3a4a5a", "#506070", "#8ea8c0", "#c8d8e8", "#e6f0fa", "#ffffff"]


# ── catalogue ────────────────────────────────────────────────────────────────
def c_literals(block: str) -> str:
    return "".join(m.group(1).replace('\\"', '"').replace("\\\\", "\\")
                   for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', block))


def entries():
    text = CATALOG.read_text(encoding="utf-8")
    body = text[text.index("// BEGIN-ENTRIES"):text.index("// END-ENTRIES")]
    out = []
    for m in re.finditer(r'\{"([^"]+)",\s*"((?:[^"\\]|\\.)*)",\s*/\*original\*/(.*?)/\*themed\*/(.*?)\},',
                         body, re.S):
        out.append((m.group(1), m.group(2), c_literals(m.group(3)), c_literals(m.group(4))))
    return out


# ── theme tokens ─────────────────────────────────────────────────────────────
def flatten(obj, prefix, out):
    for k, v in obj.items():
        key = f"{prefix}.{k}" if prefix else k
        if isinstance(v, dict) and "type" not in v and "family" not in v:
            flatten(v, key, out)
        else:
            out[key] = v


def theme_tokens(path: Path):
    d = json.loads(path.read_text(encoding="utf-8"))
    toks = {}
    flatten(d["scopes"]["root"]["tokens"], "", toks)
    prim = d.get("primitives", {})
    lit = d.get("literalRemap")
    return d, toks, prim, lit


def resolve_value(v, prim, base_toks, base_prim):
    if isinstance(v, str) and v.startswith("{") and v.endswith("}"):
        k = v[1:-1]
        if k in prim:
            return prim[k]
        return base_prim[k]
    return v


# ── ThemeManager::remapLiteralColor port ─────────────────────────────────────
def hx(c):
    c = c.lstrip("#")
    return tuple(int(c[i:i + 2], 16) / 255 for i in (0, 2, 4))


def hsl(rgb):
    h, l, s = colorsys.rgb_to_hls(*rgb)
    mx, mn = max(rgb), min(rgb)
    hue = -1.0 if mx == mn else h * 360.0
    return hue, s, l


def lerp(a, b, t):
    t = min(max(t, 0.0), 1.0)
    return tuple(_f32(a[i] + (b[i] - a[i]) * t) for i in range(3))


def remap_color(rgb, neutral, accent):
    src_rgb = [hx(c) for c in SRC_NEUTRAL]
    src_l = [hsl(c)[2] for c in src_rgb]
    hue, s, l = hsl(rgb)
    on_ramp = any(all(round(a * 255) == round(b * 255) for a, b in zip(rgb, c)) for c in src_rgb)
    if on_ramp or s < 0.12 or (s < 0.55 and 190.0 <= hue <= 260.0):
        if l <= src_l[0]:
            return neutral[0]
        if l >= src_l[-1]:
            return neutral[-1]
        out = neutral[-1]
        for i in range(len(src_l) - 1):
            if src_l[i] <= l <= src_l[i + 1]:
                span = src_l[i + 1] - src_l[i]
                out = lerp(neutral[i], neutral[i + 1], (l - src_l[i]) / span if span > 0 else 0.0)
                break
        return out
    if 170.0 <= hue <= 215.0 and s >= 0.55:
        p = (0.25, 0.45, 0.75)
        if l <= p[0]:
            return accent[0]
        if l >= p[2]:
            return accent[2]
        if l <= p[1]:
            return lerp(accent[0], accent[1], (l - p[0]) / (p[1] - p[0]))
        return lerp(accent[1], accent[2], (l - p[1]) / (p[2] - p[1]))
    return rgb


def _f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def _q8(v):
    # QColor::fromRgbF stores qRound(v * 65535); name() reads qt_div_257().
    x = int(_f32(v) * 65535 + 0.5) if v >= 0 else 0
    x = min(max(x, 0), 65535)
    return (x - (x >> 8) + 0x80) >> 8


def name(rgb):
    return "#" + "".join(f"{_q8(v):02x}" for v in rgb)


def remap_hex(c, lit):
    if not lit:
        return c
    neutral = [hx(x) for x in lit["neutral"]]
    accent = [hx(x) for x in lit["accent"]]
    return name(remap_color(hx(c), neutral, accent))


HEX6 = re.compile(r"#([0-9a-fA-F]{6})(?![0-9a-fA-F])")
TOKEN = re.compile(r"\{\{([^}]+)\}\}")


def resolve_sheet(tmpl, toks, prim, base_toks, base_prim, lit):
    tmpl = HEX6.sub(lambda m: remap_hex("#" + m.group(1), lit), tmpl)

    def tok(m):
        k = m.group(1).strip()
        v = toks.get(k, base_toks.get(k))
        if v is None:
            raise KeyError(k)
        return str(resolve_value(v, prim, base_toks, base_prim))
    return TOKEN.sub(tok, tmpl)


def close(a, b):
    if a == b:
        return True
    ha, hb = HEX6.findall(a), HEX6.findall(b)
    if HEX6.sub("#", a) != HEX6.sub("#", b) or len(ha) != len(hb):
        return False
    for x, y in zip(ha, hb):
        if any(abs(int(x[i:i + 2], 16) - int(y[i:i + 2], 16)) > 1 for i in (0, 2, 4)):
            return False
    return True


def main():
    write = "--write" in sys.argv
    _, base_toks, base_prim, _ = theme_tokens(THEMES / BASE)
    ents = entries()
    if not ents:
        print("no entries parsed"); return 1
    bad = 0
    # Write the remapped color.button.* values first, so the check sees them.
    if write:
        base_doc = json.loads((THEMES / BASE).read_text(encoding="utf-8"))
        base_btn = base_doc["scopes"]["root"]["tokens"]["color"]["button"]
        groups = {g: v for g, v in base_btn.items() if isinstance(v, dict)}   # the catalogue's groups
        for tf in REMAP_THEMES:
            d, toks, prim, lit = theme_tokens(THEMES / tf)
            btn = d["scopes"]["root"]["tokens"].setdefault("color", {}).setdefault("button", {})
            for grp, leaves in groups.items():
                # A primitive reference ("{color.gray.800}", build 772) stands
                # for a generic token the original sheet used: the remap theme
                # resolves it to its own primitive, so it is copied as it is.
                btn[grp] = {leaf: (v if isinstance(v, str) and v.startswith("{")
                                   else remap_hex(resolve_value(v, base_prim, base_toks, base_prim), lit))
                            for leaf, v in leaves.items()}
            (THEMES / tf).write_text(json.dumps(d, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            print(f"wrote color.button.{{{','.join(groups)}}} into {tf}")
    for tf in [BASE] + REMAP_THEMES:
        _, toks, prim, lit = theme_tokens(THEMES / tf)
        for eid, site, orig, themed in ents:
            a = resolve_sheet(orig, toks, prim, base_toks, base_prim, lit)
            b = resolve_sheet(themed, toks, prim, base_toks, base_prim, lit)
            ok = (a == b) if tf == BASE else close(a, b)
            if not ok:
                bad += 1
                print(f"MISMATCH {tf} {eid}:\n  original: {a}\n  themed:   {b}")
    print(f"{len(ents)} entries x {1 + len(REMAP_THEMES)} themes: "
          f"{'all identical' if not bad else str(bad) + ' mismatch(es)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
