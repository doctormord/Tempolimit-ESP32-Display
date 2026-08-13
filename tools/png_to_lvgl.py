#!/usr/bin/env python3
"""
png_to_lvgl.py - stellt ein zweifarbiges Piktogramm frei und schreibt es als
LVGL-Bild im Format A8 (nur Deckkraft, keine Farbe).

    python3 tools/png_to_lvgl.py src/fahrrad.png src/img_fahrrad.c \\
            --name img_fahrrad --polarity hell --height 131

Warum A8 und nicht RGB565: gespeichert wird nur die Deckkraft, ein Byte je
Pixel statt zwei. Die Farbe kommt beim Zeichnen aus dem Stil
(`lv_obj_set_style_image_recolor`), das Piktogramm laesst sich damit weiss auf
blau und ebenso in jeder anderen Farbe darstellen, ohne es neu zu erzeugen.

--polarity hell   = helle Flaeche ist das Motiv (weisses Fahrrad auf blau)
--polarity dunkel = dunkle Flaeche ist das Motiv (schwarze Figuren auf weiss)

Das Motiv wird auf seinen Inhalt beschnitten und mit Kantenglaettung auf die
Zielhoehe skaliert - deshalb A8 und nicht 1 Bit.
"""

import argparse
import os

import numpy as np
from PIL import Image


def convert(src, dst, name, polarity, height, threshold):
    im = Image.open(src)
    if im.mode == "RGBA":
        # Transparente Bereiche zaehlen als Hintergrund
        bg = Image.new("RGBA", im.size, (255, 255, 255, 255))
        im = Image.alpha_composite(bg, im)
    g = np.asarray(im.convert("L")).astype(np.int16)

    # Deckkraft: 255 wo das Motiv ist
    alpha = (g - threshold) if polarity == "hell" else (threshold - g)
    alpha = np.clip(alpha * (255.0 / max(threshold, 255 - threshold)), 0, 255)

    ys, xs = np.where(alpha > 8)
    if len(xs) == 0:
        raise SystemExit(f"{src}: kein Motiv gefunden - Polaritaet falsch?")
    a = alpha[ys.min():ys.max() + 1, xs.min():xs.max() + 1]

    h0, w0 = a.shape
    w = max(1, round(w0 * height / h0))
    img = Image.fromarray(a.astype(np.uint8), "L").resize((w, height),
                                                          Image.LANCZOS)
    data = np.asarray(img).astype(np.uint8)

    body = []
    for row in data:
        body.append("    " + " ".join(f"0x{v:02x}," for v in row))

    with open(dst, "w") as f:
        f.write(f'''/*
 * {os.path.basename(dst)} - erzeugt von tools/png_to_lvgl.py
 * Quelle: {os.path.basename(src)}, freigestellt und auf {w}x{height} skaliert.
 *
 * Format A8: ein Byte Deckkraft je Pixel, keine Farbe. Die Farbe kommt beim
 * Zeichnen aus dem Stil (image_recolor) - deshalb laesst sich dasselbe Bild
 * weiss auf blau und in jeder anderen Farbe darstellen.
 *
 * Nicht von Hand aendern, sondern das Werkzeug neu laufen lassen.
 */

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

static const uint8_t {name}_map[] = {{
''')
        f.write("\n".join(body))
        f.write(f'''
}};

const lv_image_dsc_t {name} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_A8,
    .header.flags = 0,
    .header.w = {w},
    .header.h = {height},
    .header.stride = {w},
    .header.reserved_2 = 0,
    .data_size = sizeof({name}_map),
    .data = {name}_map,
    .reserved = NULL,
}};
''')
    print(f"{dst}: {w}x{height} px, {w*height/1024:.1f} KiB "
          f"(Original {w0}x{h0})")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--name", required=True)
    ap.add_argument("--polarity", choices=("hell", "dunkel"), required=True)
    ap.add_argument("--height", type=int, default=131)
    ap.add_argument("--threshold", type=int, default=128)
    a = ap.parse_args()
    convert(a.src, a.dst, a.name, a.polarity, a.height, a.threshold)


if __name__ == "__main__":
    main()
