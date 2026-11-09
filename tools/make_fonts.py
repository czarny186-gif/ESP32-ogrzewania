#!/usr/bin/env python3
"""
Generator czcionek VLW (format "Smooth Font" z Processing / TFT_eSPI / LovyanGFX)
z plików TTF, z polskimi znakami. Wynik: nagłówki C z tablicami w pamięci Flash.

Użycie:  python3 tools/make_fonts.py <katalog_z_ttf> src/fonts
Wymaga:  pip install freetype-py
Czcionka: Barlow Semi Condensed (SIL Open Font License) – Google Fonts.
"""
import struct
import sys
import os
import freetype

ASCII = "".join(chr(c) for c in range(32, 127))
POLISH = "ąćęłńóśźżĄĆĘŁŃÓŚŹŻ"
EXTRA = "°–—−·×…"
FULL = ASCII + POLISH + EXTRA
DIGITS = "0123456789.,-+:°C "

FONTS = [
    # nazwa      plik                               px   znaki
    ("FontS",   "BarlowSemiCondensed-Medium.ttf",   16, FULL),
    ("FontM",   "BarlowSemiCondensed-Medium.ttf",   20, FULL),
    ("FontB",   "BarlowSemiCondensed-SemiBold.ttf", 26, FULL),
    ("FontXL",  "BarlowSemiCondensed-SemiBold.ttf", 40, FULL),
    ("FontBig", "BarlowSemiCondensed-Medium.ttf",   96, DIGITS),
]


def make_vlw(ttf, size, chars):
    face = freetype.Face(ttf)
    face.set_pixel_sizes(0, size)
    ascent = face.size.ascender >> 6
    descent = -(face.size.descender >> 6)
    glyphs = []
    for ch in sorted(set(chars), key=ord):
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        g = face.glyph
        bm = g.bitmap
        w, h = bm.width, bm.rows
        data = bytearray()
        for row in range(h):
            data += bytes(bm.buffer[row * bm.pitch: row * bm.pitch + w])
        glyphs.append(dict(code=ord(ch), w=w, h=h, adv=(g.advance.x + 32) >> 6,
                           top=g.bitmap_top, left=g.bitmap_left, data=bytes(data)))
    # Ciasne metryki: linia bazowa = najwyższy glif, aby układ na ekranie
    # i w symulacji (tools/../docs/symulacja.html) był identyczny.
    ascent = max(g["top"] for g in glyphs)
    descent = max(g["h"] - g["top"] for g in glyphs)
    out = bytearray()
    out += struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent)
    for g in glyphs:
        out += struct.pack(">7i", g["code"], g["h"], g["w"], g["adv"], g["top"], g["left"], 0)
    for g in glyphs:
        out += g["data"]
    return bytes(out), ascent, descent, ascent


def main():
    src = sys.argv[1]
    dst = sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for name, fn, px, chars in FONTS:
        data, asc, desc, top = make_vlw(os.path.join(src, fn), px, chars)
        lines = [f"// {name}: {fn} {px}px, {len(set(chars))} znaków, {len(data)} B (wygenerowane przez tools/make_fonts.py)",
                 "#pragma once", "#include <stdint.h>",
                 f"static const uint8_t {name}[] = {{"]
        for i in range(0, len(data), 24):
            lines.append("  " + ",".join(f"0x{b:02X}" for b in data[i:i + 24]) + ",")
        lines.append("};")
        with open(os.path.join(dst, f"{name}.h"), "w") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{name:8s} {px:3d}px ascent={asc} descent={desc} maxTop={top} size={len(data)}")


if __name__ == "__main__":
    main()
