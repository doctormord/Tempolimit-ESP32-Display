#!/usr/bin/env python3
"""
even_digit_spacing.py - gives every digit of an LVGL font the same side
bearing, so the gaps between any two digits come out equal.

    python3 tools/even_digit_spacing.py src/lv_font_din_m162.c

Why:
DIN 1451 has tabular digits - all with the same advance width. But the
stroke widths differ a lot (the 1 is half as wide as the 8), and the side
bearings swing from 1 to 8 px. That shows up as uneven gaps:

    100 -> 11.8 / 11.8      even
    120 -> 10.8 / 12.8
    130 ->  7.8 / 13.8      the 3 sits right up against the 1

For a table, equal advance width is correct; for a single large number,
equal optical spacing is what matters. This tool therefore sets ofs_x and
adv_w on every digit so the same gap sits on both sides - turning the digits
proportional instead of tabular.

The bitmaps are left untouched. Re-run after every lv_font_conv pass.
"""

import argparse
import re
import sys


def cmap_index(src, codepoint):
    """
    cmap_index(src, codepoint) - glyph index of a character.

    Parameters:
      src       - full text of the generated font .c file
      codepoint - character code to look up

    Covers both cmap forms that lv_font_conv emits: a contiguous range and
    an explicit list. Returns None if the codepoint isn't in the font.
    """
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
    """
    glyphs(src) - extract (adv_w, box_w, ofs_x) for every glyph in the font,
    in glyph-index order.

    Parameters:
      src - full text of the generated font .c file
    """
    blk = re.search(r"glyph_dsc\[\] = \{(.*?)\n\};", src, re.S)
    return re.findall(
        r"\{\.bitmap_index = \d+, \.adv_w = (\d+), \.box_w = (\d+), "
        r"\.box_h = \d+, \.ofs_x = (-?\d+), \.ofs_y = -?\d+\}", blk.group(1))


def main(path, side):
    """
    main(path, side) - rewrite one font file in place so every digit 0-9
    has the same left/right side bearing.

    Parameters:
      path - path to the LVGL font .c file to modify
      side - desired side bearing in pixels, or None to auto-pick the
             median of the digits' current bearings (keeps the overall
             number width roughly unchanged)

    Locates the digit glyph entries via cmap_index()/glyphs(), computes the
    before/after width of a few representative strings for the printed
    report, then replaces each digit's .adv_w and .ofs_x by index (not by
    text pattern - several digits share identical metrics and would be
    indistinguishable by pattern match) and writes the file back.
    """
    src = open(path).read()
    rows = glyphs(src)

    idx = {}
    for d in "0123456789":
        g = cmap_index(src, ord(d))
        if g is not None and g < len(rows):
            idx[d] = g
    if len(idx) < 2:
        print(f"{path}: no digits in this character set - skipped")
        return

    # Default value: median of the current bearings, so the number's
    # overall width stays roughly the same as before
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

    # Replace entries by their index, not by a text pattern: several digits
    # share the same metrics and would not be distinguishable that way.
    blk = re.search(r"(glyph_dsc\[\] = \{)(.*?)(\n\};)", src, re.S)
    entries = list(re.finditer(
        r"\{\.bitmap_index = \d+, \.adv_w = \d+, \.box_w = \d+, "
        r"\.box_h = \d+, \.ofs_x = -?\d+, \.ofs_y = -?\d+\}", blk.group(2)))
    body = blk.group(2)
    changed = 0
    for d, g in sorted(idx.items(), key=lambda kv: -kv[1]):   # back to front
        adv_old, box_w, ofs_old = (int(x) for x in rows[g])
        adv_new = (box_w + 2 * side) * 16
        e = entries[g]
        new = re.sub(r"\.adv_w = \d+", f".adv_w = {adv_new}", e.group(0))
        new = re.sub(r"\.ofs_x = -?\d+", f".ofs_x = {side}", new)
        body = body[:e.start()] + new + body[e.end():]
        changed += 1
    out = src[:blk.start(2)] + body + src[blk.end(2):]

    open(path, "w").write(out)
    print(f"{path}: side bearing of all digits set to {side} px "
          f"(gap {2*side} px between any two digits)")
    for t in sorted(before):
        print(f"     {t}: {before[t]:5.0f} px -> {width(t):5.0f} px")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--side", type=int, default=None,
                    help="side bearing in pixels (default: median of the current ones)")
    a = ap.parse_args()
    for f in a.files:
        main(f, a.side)
