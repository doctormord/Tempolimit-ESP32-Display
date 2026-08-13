#!/usr/bin/env python3
"""
even_digit_spacing.py - gibt allen Ziffern eines LVGL-Fonts denselben
Seitenabstand, damit die Luecken zwischen beliebigen Ziffern gleich sind.

    python3 tools/even_digit_spacing.py src/lv_font_din_m162.c

Warum:
DIN 1451 hat Tabellenziffern - alle mit demselben Vorschub. Die Strichbreiten
unterscheiden sich aber stark (die 1 ist halb so breit wie die 8), und die
Seitenabstaende schwanken von 1 bis 8 px. Sichtbar wird das an ungleichen
Luecken:

    100 -> 11,8 / 11,8      gleichmaessig
    120 -> 10,8 / 12,8
    130 ->  7,8 / 13,8      die 3 klebt an der 1

Fuer eine Tabelle ist gleicher Vorschub richtig, fuer eine einzelne grosse
Zahl zaehlt der gleiche optische Abstand. Das Werkzeug setzt deshalb bei jeder
Ziffer ofs_x und adv_w so, dass links und rechts derselbe Abstand steht - die
Ziffern werden dadurch proportional statt tabellarisch.

Die Bitmaps bleiben unberuehrt. Nach jedem Lauf von lv_font_conv erneut
aufrufen.
"""

import argparse
import re
import sys


def cmap_index(src, codepoint):
    """Glyphenindex eines Zeichens. Deckt beide cmap-Formen ab, die
    lv_font_conv erzeugt: zusammenhaengender Bereich und Liste."""
    for m in re.finditer(r"\.range_start = (\d+), \.range_length = (\d+), "
                         r"\.glyph_id_start = (\d+)", src):
        start, length, gid0 = (int(m.group(i)) for i in (1, 2, 3))
        if not (start <= codepoint < start + length):
            continue
        lst = re.search(r"unicode_list_0\[\] = \{(.*?)\};", src, re.S)
        if not lst or codepoint >= 128:
            return gid0 + (codepoint - start)
        vals = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", lst.group(1))]
        want = codepoint - start
        if want in vals:
            return gid0 + vals.index(want)
    return None


def glyphs(src):
    blk = re.search(r"glyph_dsc\[\] = \{(.*?)\n\};", src, re.S)
    return re.findall(
        r"\{\.bitmap_index = \d+, \.adv_w = (\d+), \.box_w = (\d+), "
        r"\.box_h = \d+, \.ofs_x = (-?\d+), \.ofs_y = -?\d+\}", blk.group(1))


def main(path, side):
    src = open(path).read()
    rows = glyphs(src)

    idx = {}
    for d in "0123456789":
        g = cmap_index(src, ord(d))
        if g is not None and g < len(rows):
            idx[d] = g
    if len(idx) < 2:
        print(f"{path}: keine Ziffern im Zeichensatz - uebersprungen")
        return

    # Vorgabewert: Median der bisherigen Abstaende, damit die Zahl insgesamt
    # ungefaehr so breit bleibt wie zuvor
    if side is None:
        b = []
        for d, g in idx.items():
            a, w, o = (int(x) for x in rows[g])
            b += [o, a / 16.0 - w - o]
        b.sort()
        side = round(b[len(b) // 2])

    def width(txt):
        return sum(int(rows[idx[c]][1]) + 2 * side for c in txt if c in idx)

    before = {t: None for t in ("100", "120", "130", "888")}
    for t in before:
        w = 0
        for c in t:
            a = int(rows[idx[c]][0]) / 16.0
            w += a
        before[t] = w

    # Eintraege ueber ihren Index ersetzen, nicht ueber ein Textmuster:
    # mehrere Ziffern haben dieselbe Metrik und waeren nicht unterscheidbar.
    blk = re.search(r"(glyph_dsc\[\] = \{)(.*?)(\n\};)", src, re.S)
    entries = list(re.finditer(
        r"\{\.bitmap_index = \d+, \.adv_w = \d+, \.box_w = \d+, "
        r"\.box_h = \d+, \.ofs_x = -?\d+, \.ofs_y = -?\d+\}", blk.group(2)))
    body = blk.group(2)
    changed = 0
    for d, g in sorted(idx.items(), key=lambda kv: -kv[1]):   # von hinten
        adv_old, box_w, ofs_old = (int(x) for x in rows[g])
        adv_new = (box_w + 2 * side) * 16
        e = entries[g]
        new = re.sub(r"\.adv_w = \d+", f".adv_w = {adv_new}", e.group(0))
        new = re.sub(r"\.ofs_x = -?\d+", f".ofs_x = {side}", new)
        body = body[:e.start()] + new + body[e.end():]
        changed += 1
    out = src[:blk.start(2)] + body + src[blk.end(2):]

    open(path, "w").write(out)
    print(f"{path}: Seitenabstand aller Ziffern auf {side} px "
          f"(Luecke {2*side} px zwischen je zwei Ziffern)")
    for t in sorted(before):
        print(f"     {t}: {before[t]:5.0f} px -> {width(t):5.0f} px")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--side", type=int, default=None,
                    help="Seitenabstand in Pixeln (Standard: Median der bisherigen)")
    a = ap.parse_args()
    for f in a.files:
        main(f, a.side)
