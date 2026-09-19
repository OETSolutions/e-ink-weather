#!/usr/bin/env python3
"""Generate 1-bit font atlases for the e-paper panel.

WHY ATLASES AND NOT A RUNTIME FONT ENGINE (FR-4a): the panel is 1-bit, so anti-aliasing
is impossible — a grey fringe becomes visible speckle at 198 dpi. The crispest text comes
from glyphs rasterised ONCE at the exact pixel size, hinted for that size, and blitted. A
runtime TTF rasteriser would also cost RAM and flash the device does not have.

THRESHOLD, DO NOT DITHER. Dithering an anti-aliased edge produces a checkerboard of dots
that reads as dirt at this resolution. A hard threshold gives a clean, slightly thinner
stroke, which is what the vendor demo's own font data does.

Output: one C header per face, containing
  - a glyph table: for each printable ASCII char, an offset, width, height, advance and
    the x/y bearing from the pen origin to the ink box (signed — glyphs routinely overhang)
  - the face's ascent/descent/line-height, so the C side cannot drift from the atlas
  - a packed 1-bit bitmap, MSB-first, row-major, pitch = ceil(w/8)
  - WHITE (0) padding bits beyond the glyph width, so the blitter can copy whole bytes
    without masking (a stray ink bit would show as a dot on the glass)

Usage:
    python3 tools/gen_font_atlas.py --ttf Inter-Regular.ttf --px 20 \
        --name FONT_BODY --out firmware/lib/layout/src/atlas_body.h
"""
import argparse
import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

# Printable ASCII. Everything the default layout can render must be present — a missing
# glyph renders as a blank, which for a temperature like "-12.4" is silently wrong.
FIRST_CHAR = 32
LAST_CHAR = 126


def rasterise(font, ch, px, ascent):
    """Rasterise one glyph.

    Returns (bitmap_rows, width, height, advance, bearing_x, bearing_y) where the bearings
    are the offset from the PEN ORIGIN to the ink box's top-left corner. Without them the
    renderer can only place glyphs by their ink box, which puts a '.' at the top of the line
    instead of on the baseline.

    bearing_y is measured from the BASELINE (negative = above it), not from the ascender
    line: Pillow's default anchor is "la" (left/ascender), so the raw bbox top is relative to
    the ascender. Returning that raw value silently pushed every digit 62px down the line at
    the 64px size — the value face rendered clipped in half.
    """
    # Render on a generous canvas and measure the ink bbox, so a glyph is not clipped by
    # its own advance width (e.g. italic overhang) or by descenders.
    pad = px
    size = px * 3
    img = Image.new("L", (size, size), 255)
    d = ImageDraw.Draw(img)
    try:
        # Default anchor "la": the pen origin is at (pad, pad), with the ASCENDER line
        # running through y = pad. Bearings are therefore measured from that point and
        # converted below.
        d.text((pad, pad), ch, font=font, fill=0)
    except Exception:
        return None

    advance = int(round(font.getlength(ch)))

    # THRESHOLD BEFORE MEASURING. getbbox() on a mode-L image returns the bbox of non-zero
    # pixels — and the background is 255, which is non-zero — so measuring first yields the
    # whole canvas for every glyph. That silently produced 192x192 "glyphs" (every
    # character a black square) and a 2.7 MB atlas. Thresholding first gives a true ink
    # bbox, and it is the same threshold used for the bitmap below, so the two cannot
    # disagree about what counts as ink.
    ink = img.point(lambda p: 255 if p < 128 else 0)
    bbox = ink.getbbox()
    if bbox is None:
        # Space and friends: no ink, but they must still advance.
        return [], 0, 0, max(advance, 1), 0, 0

    l, t, r, b = bbox
    w, h = r - l, b - t

    # Pack MSB-first, row-major, with white padding in the last byte.
    pitch = (w + 7) // 8
    rows = []
    for y in range(h):
        byte = 0
        nbits = 0
        rowbytes = []
        for x in range(w):
            bit = 1 if ink.getpixel((l + x, t + y)) else 0
            byte = (byte << 1) | bit
            nbits += 1
            if nbits == 8:
                rowbytes.append(byte)
                byte = 0
                nbits = 0
        if nbits:                          # pad the final partial byte with white
            rowbytes.append(byte << (8 - nbits))
        assert len(rowbytes) == pitch
        rows.extend(rowbytes)
    return rows, w, h, advance, l - pad, (t - pad) - ascent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", required=True)
    ap.add_argument("--px", type=int, required=True, help="pixel size to rasterise at")
    ap.add_argument("--name", required=True, help="macro prefix, e.g. FONT_BODY")
    ap.add_argument("--out", required=True)
    ap.add_argument("--index", type=int, default=0, help="face index in a .ttc")
    args = ap.parse_args()

    if not os.path.exists(args.ttf):
        sys.exit(f"font not found: {args.ttf}")

    font = ImageFont.truetype(args.ttf, args.px, index=args.index)
    try:
        font.getlength("x")     # required by rasterise(); absent on ancient Pillow
    except AttributeError:
        sys.exit("this Pillow is too old: ImageFont.getlength is required")

    # The face's own ascent/descent, NOT the tallest glyph's ink box. Emitted into the
    # header so fonts.c cannot drift from the atlas: if these were hardcoded in C, changing
    # --px here would silently leave the C metrics stale, and every line of text would sit
    # at the wrong baseline.
    ascent, descent = font.getmetrics()

    glyphs = []
    blob = []
    for ch in range(FIRST_CHAR, LAST_CHAR + 1):
        g = rasterise(font, chr(ch), args.px, ascent)
        if g is None:
            sys.exit(f"failed to rasterise {chr(ch)!r}")
        rows, w, h, adv, bx, by = g
        off = len(blob)
        blob.extend(rows)
        glyphs.append((off, w, h, adv, bx, by))

    # The blitter indexes glyphs as (ch - FONT_FIRST_CHAR), so a mismatch here would shift
    # every character by one — the kind of bug that renders plausible-looking garbage.
    assert len(glyphs) == LAST_CHAR - FIRST_CHAR + 1

    # The packed blob must be exactly as long as the glyph table claims, or the renderer
    # would read past the end of the array.
    assert len(blob) == sum(((w + 7) // 8) * h for (_, w, h, _, _, _) in glyphs)

    with open(args.out, "w") as fh:
        fh.write(f"// GENERATED by tools/gen_font_atlas.py — DO NOT EDIT.\n")
        fh.write(f"// face: {os.path.basename(args.ttf)} @ {args.px}px, "
                 f"thresholded to 1 bpp (no dithering, per FR-4a)\n")
        fh.write(f"#pragma once\n#include <stdint.h>\n\n")
        fh.write(f"#define {args.name}_FIRST_CHAR {FIRST_CHAR}\n")
        fh.write(f"#define {args.name}_LAST_CHAR  {LAST_CHAR}\n")
        fh.write(f"#define {args.name}_PX        {args.px}\n")
        fh.write(f"#define {args.name}_GLYPH_COUNT {len(glyphs)}\n")
        fh.write(f"#define {args.name}_ASCENT   {ascent}\n")
        fh.write(f"#define {args.name}_DESCENT  {descent}\n")
        fh.write(f"#define {args.name}_LINE_HEIGHT {ascent + descent}\n\n")
        # bx/by are signed: glyphs routinely have a negative left bearing.
        fh.write(f"typedef struct {{ uint32_t off; uint8_t w, h; uint8_t advance; "
                 f"int8_t bx, by; }} {args.name.lower()}_glyph_t;\n\n")
        fh.write(f"static const {args.name.lower()}_glyph_t {args.name}_GLYPHS"
                 f"[{args.name}_GLYPH_COUNT] = {{\n")
        for (off, w, h, adv, bx, by) in glyphs:
            fh.write(f"    {{ {off}u, {w}, {h}, {adv}, {bx}, {by} }},\n")
        fh.write("};\n\n")
        fh.write(f"static const uint8_t {args.name}_BITS[{max(len(blob), 1)}] = {{\n")
        for i in range(0, len(blob), 16):
            chunk = blob[i:i + 16]
            fh.write("    " + " ".join(f"0x{b:02X}," for b in chunk) + "\n")
        fh.write("};\n")

    print(f"wrote {args.out}: {len(glyphs)} glyphs, {len(blob)} bitmap bytes "
          f"({args.px}px)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
