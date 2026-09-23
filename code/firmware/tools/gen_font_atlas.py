#!/usr/bin/env python3
"""Generate the 1-bit font LADDER for the e-paper panel.

WHY ATLASES AND NOT A RUNTIME FONT ENGINE (FR-4a): the panel is 1-bit, so anti-aliasing
is impossible — a grey fringe becomes visible speckle at 198 dpi. The crispest text comes
from glyphs rasterised ONCE at the exact pixel size, hinted for that size, and blitted. A
runtime TTF rasteriser would also cost RAM and flash the device does not have.

THRESHOLD, DO NOT DITHER. Dithering an anti-aliased edge produces a checkerboard of dots
that reads as dirt at this resolution. A hard threshold gives a clean, slightly thinner
stroke, which is what the vendor demo's own font data does.

WHY A LADDER IN ONE HEADER RATHER THAN ONE FILE PER FACE: FR-4a calls for a "fixed ladder of
sizes tuned to 198 dpi", and Q10 left the exact ladder open. The first cut shipped only two
faces (20 px and 64 px) because each face was a separate hand-invoked generator run with its
own type names — adding a size meant editing the C glue too, so it never happened. Emitting
every face into ONE header with ONE struct shape makes the ladder data-driven: the C side is
a table lookup with no per-face code, and a size is added by putting a number in --ladder.

WHY THE RASTERISED LADDER STOPS AT 128 px. Three independent limits agree on it:

  1. THE int8_t y BEARING. `atlas_glyph_t.by` is one signed byte. The deepest ink is about
     -(px - descent), so a face past ~128 px would push a bearing below -128, where it WRAPS
     to a positive value and places the glyph BELOW the baseline — legible-but-wrong text, not
     a crash, which is the worst kind of atlas bug. The size check in main() enforces this.
  2. FLASH, and it is double-counted. Every face ships TWICE — once as a C array and once in
     the web app's bundle (NFR-4: the preview must not disagree with the panel) — and both
     copies live in the 1.9 MB app slot. Cost grows with px^2: the 128 px face alone is 64 KB,
     a 160 px face is 99 KB, and by 192 px the pair no longer fits at all. Measured on the
     ten-face ladder: +214 KB firmware (92.6% of the slot) and +54 KB gzipped bundle (80.8% of
     the 160 KiB budget), leaving ~147 KB and ~37 KiB of headroom respectively.
  3. THE USEFUL RANGE. The 96 -> 128 step is 1.33x, the same ratio as every step below it, so
     nothing a user would want is missing between the body face and the ceiling.

SIZES ABOVE 128 px ARE NOT RASTERISED — THEY ARE UPSCALED (--upscale). A rasterised 512 px
face is 910 KB, which cannot fit in a 1.9 MB app slot that already holds 214 KB of ladder; the
naive "just extend the ladder" answer is physically impossible, not merely large. But the panel
is 1 bit per pixel, so an integer BLOCK SCALE is EXACT: every pixel of an N px glyph becomes a
k x k block, and there is no grey to lose or invent. So 512 px is 128 px drawn at 4x, 384 is
128 at 3x, 256 is 128 at 2x — six new sizes for ZERO extra bitmap bytes, because they REUSE
the base face's glyph table and bitmap and only multiply the metrics.

WHAT A BLOCK SCALE COSTS IN QUALITY, HONESTLY: an upscaled glyph is the 1x face's hinting
stretched, so its stroke weights are k times the base rather than separately hinted for the
size. At 1 bit that is a legitimate, crisp look (the same thing draw_line_scaled() in
provscreen.c already does for the provisioning screen), and it is the ONLY way to reach these
sizes at all. A user who needs differently-hinted 512 px text does not get it; a user who needs
512 px text on the glass does. The metadata travels in atlas_face_t (upscale, upscale_of) and in
the web app's Face (upscale, upscaleFromPx) so BOTH renderers scale identically (NFR-4).

Regular is used below --bold-from and SemiBold at and above it: the hero numerals want weight
and the small labels do not.

Output: TWO headers.
  - atlas_ladder.h (small, included by fonts.h): the ladder X-macro, the counts, the structs.
  - the --out data header (included by fonts.c alone): per face,
      - a glyph table: for each printable ASCII char, an offset, width, height, advance and the
        x/y bearing from the pen origin to the ink box (signed — glyphs routinely overhang)
      - the face's ascent/descent/line-height, so the C side cannot drift from the atlas
      - a packed 1-bit bitmap, MSB-first, row-major, pitch = ceil(w/8)
      - WHITE (0) padding bits beyond the glyph width, so the blitter can copy whole bytes
        without masking (a stray ink bit would show as a dot on the glass)

Usage:
    python3 tools/gen_font_atlas.py \\
        --ttf-regular assets/fonts/Inter-Regular.ttf \\
        --ttf-bold    assets/fonts/Inter-SemiBold.ttf \\
        --ladder 16,20,24,32,40,48,64,80,96,128 \\
        --upscale 160:80:2,192:96:2,256:128:2,320:80:4,384:128:3,512:128:4 \\
        --out        lib/layout/src/atlas.h \\
        --ladder-out lib/layout/include/atlas_ladder.h \\
        --extra '°'

--ladder lists the sizes that are RASTERISED; --upscale adds sizes that are k x BLOCK SCALES of
an already-rasterised size (`px:base:k`), for zero extra bitmap bytes. The upscaled size must be
exactly base * k and must not also appear in --ladder.

TWO OUTPUTS, DELIBERATELY. The ladder-shape header (sizes, counts, structs) is tiny and is
included by fonts.h, so anything asking for a font id gets it cheaply. The DATA header is
~1.1 MB of source for ~170 KB of bitmap and is included by fonts.c ALONE; putting it in
fonts.h would have forced every translation unit to carry the atlas.

The degree sign U+00B0 is the ONLY character outside printable ASCII this project needs, and
it needs it badly: every imperial temperature is written "68.4°F", and a missing glyph does not
degrade gracefully — font_measure() fails and the renderer draws NOTHING, so the panel would
show a blank where the temperature belongs. It is rasterised separately and placed after the
ASCII glyphs, with a small table mapping it back to its codepoint.

WHY NOT EXTEND THE RANGE TO COVER 176: the glyph index is (codepoint - FIRST_CHAR) and every
intervening glyph would have to be stored, so covering U+00B0 the naive way adds 80 mostly
empty glyphs to EVERY face — over 100 KB of flash. The side table costs one entry per
non-ASCII character instead.
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
GLYPH_COUNT = LAST_CHAR - FIRST_CHAR + 1


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


def build_face(ttf, px, extra_spec):
    """Rasterise every glyph for one face. Returns a dict, or exits on failure."""
    if not os.path.exists(ttf):
        sys.exit(f"font not found: {ttf}")
    font = ImageFont.truetype(ttf, px)
    try:
        font.getlength("x")     # required by rasterise(); absent on ancient Pillow
    except AttributeError:
        sys.exit("this Pillow is too old: ImageFont.getlength is required")

    # The face's own ascent/descent, NOT the tallest glyph's ink box. Emitted into the
    # header so fonts.c cannot drift from the atlas: if these were hardcoded in C, changing
    # a size would silently leave the C metrics stale, and every line of text would sit at
    # the wrong baseline.
    ascent, descent = font.getmetrics()

    glyphs = []
    blob = []
    for ch in range(FIRST_CHAR, LAST_CHAR + 1):
        g = rasterise(font, chr(ch), px, ascent)
        if g is None:
            sys.exit(f"failed to rasterise {chr(ch)!r} at {px}px")
        rows, w, h, adv, bx, by = g
        off = len(blob)
        blob.extend(rows)
        glyphs.append((off, w, h, adv, bx, by))

    # Non-ASCII glyphs, appended AFTER the ASCII block so every existing index is unchanged.
    extra = []
    for spec in (extra_spec or ""):
        cp = ord(spec)
        if cp < FIRST_CHAR or cp > LAST_CHAR:
            g = rasterise(font, spec, px, ascent)
            if g is None:
                sys.exit(f"failed to rasterise {spec!r} (U+{cp:04X}) at {px}px")
            rows, w, h, adv, bx, by = g
            off = len(blob)
            blob.extend(rows)
            extra.append((cp, off, w, h, adv, bx, by))

    # The packed blob must be exactly as long as the glyph table claims, or the renderer
    # would read past the end of the array.
    assert len(blob) == sum(((w + 7) // 8) * h for (_, w, h, _, _, _) in glyphs) \
                         + sum(((w + 7) // 8) * h for (_, _, w, h, _, _, _) in extra)
    assert len(glyphs) == GLYPH_COUNT

    return {
        "ttf": os.path.basename(ttf), "px": px, "ascent": ascent, "descent": descent,
        "glyphs": glyphs, "extra": extra, "blob": blob, "upscale": 1,
    }


def build_upscaled(px, base, k):
    """Describe a face that is `k` x BLOCK SCALES of a rasterised face, for no bitmap bytes.

    It carries NO glyph table and NO bitmap of its own: it points at the base face's arrays and
    records the factor. THE METRICS STORED HERE ARE THE BASE'S, UNSCALED — every accessor in
    fonts.c returns values that describe the base buffer, so that a glyph's returned w/h always
    matches the bitmap the returned pointer addresses, and the scale factor is applied by the
    two renderers (which are the only code that lays text out and blits). font_px() still reports
    the nominal size, and font_scale() reports k."""
    return {
        "ttf": base["ttf"], "px": px, "ascent": base["ascent"], "descent": base["descent"],
        "line_height": base["ascent"] + base["descent"],
        "glyphs": base["glyphs"], "extra": base["extra"], "blob": base["blob"],
        "upscale": k, "upscale_of": base["px"],
    }


def emit_ladder(out_path, faces):
    """Emit the tiny ladder header: the sizes, the counts, and the struct shapes.

    WHY THIS IS SEPARATE FROM THE DATA HEADER: fonts.h needs font_id_t and the glyph structs,
    but the data header is ~170 KB of bitmap and is included by fonts.c ALONE. If fonts.h pulled
    in the data, every translation unit that asks for a font id would carry the whole atlas, and
    the link would either bloat or (with the arrays static) silently duplicate it per unit. So
    the SHAPE lives here, is cheap, and is safe for a wide include; the DATA stays private. """
    L = []
    L.append("// GENERATED by tools/gen_font_atlas.py — DO NOT EDIT. See that file for why.")
    L.append("//")
    L.append("// The LADDER SHAPE only: the sizes, the counts, and the glyph structs. The bitmap")
    L.append("// data is in atlas.h, which only fonts.c includes — pulling 170 KB of glyphs into")
    L.append("// every translation unit that wants a font id would be a needless duplication.")
    L.append("#pragma once")
    L.append("#include <stdint.h>")
    L.append("")
    L.append(f"#define ATLAS_FIRST_CHAR  {FIRST_CHAR}")
    L.append(f"#define ATLAS_LAST_CHAR   {LAST_CHAR}")
    L.append(f"#define ATLAS_GLYPH_COUNT {GLYPH_COUNT}")
    L.append(f"#define ATLAS_FACE_COUNT  {len(faces)}")
    L.append("")
    L.append("/* The ladder, ascending, as an X-macro so the enum and the table are built from ONE")
    L.append(" * list. A size added here appears in font_id_t and in ATLAS_FACES automatically, and")
    L.append(" * cannot exist in one and be missing from the other. */")
    L.append("#define ATLAS_LADDER(X) \\")
    for f in faces:
        L.append(f"    X({f['px']}) \\")
    L.append("    /* end */")
    L.append("")
    L.append("/* One glyph's metrics. IDENTICAL for every face — the faces differ in their")
    L.append(" * bitmaps and their ascent/descent, not in the shape of their tables, so a single")
    L.append(" * struct lets the C side be a table lookup instead of per-face code. */")
    L.append("typedef struct { uint32_t off; uint8_t w, h; uint8_t advance; int8_t bx, by; }"
             " atlas_glyph_t;")
    L.append("typedef struct { uint32_t codepoint; uint32_t off; uint8_t w, h; uint8_t advance;"
             " int8_t bx, by; } atlas_extra_t;")
    L.append("")
    L.append("/* One rasterised size. `extra` is NULL when the face has no non-ASCII glyphs.")
    L.append(" *")
    L.append(" * `upscale` is 1 for a rasterised face and k for a face drawn as a k x k BLOCK SCALE")
    L.append(" * of another one (see the generator). An upscaled face points its `glyphs`/`bits`/")
    L.append(" * `extra` at its BASE face's arrays and stores only the factor; ascent, descent and")
    L.append(" * line_height are already multiplied by it, and the accessors scale the per-glyph")
    L.append(" * metrics. `upscale_of` is the base size in px, for the web app to resolve the reuse. */")
    L.append("typedef struct {")
    L.append("    int px, ascent, descent, line_height;")
    L.append("    const atlas_glyph_t *glyphs;")
    L.append("    const uint8_t       *bits;")
    L.append("    const atlas_extra_t *extra;")
    L.append("    int extra_count;")
    L.append("    int upscale;        /* 1, or k for a k x k block scale of `upscale_of` */")
    L.append("    int upscale_of;     /* the base size in px; equal to px when upscale == 1 */")
    L.append("} atlas_face_t;")
    L.append("")
    open(out_path, "w").write("\n".join(L) + "\n")
    print(f"wrote {out_path}: ladder shape, {len(faces)} faces")


def emit_data(out_path, faces, bold_from):
    """Emit the bulky data header, included by fonts.c alone."""
    def w(fh, s=""):
        fh.write(s + "\n")

    ladder = " ".join(str(f["px"]) for f in faces)
    with open(out_path, "w") as fh:
        w(fh, "// GENERATED by tools/gen_font_atlas.py — DO NOT EDIT.")
        w(fh, f"// {len(faces)} faces of Inter, rasterised at 198 dpi and thresholded to 1 bpp")
        w(fh, "// (no dithering, per FR-4a).")
        w(fh, f"// Ladder (px): {ladder}")
        w(fh, f"// {os.path.basename(faces[0]['ttf'])} below {bold_from}px, "
              f"{os.path.basename(faces[-1]['ttf'])} at and above it.")
        w(fh, "#pragma once")
        w(fh, '#include "atlas_ladder.h"')
        w(fh)

        for f in faces:
            if f["upscale"] != 1:
                continue        # an upscaled face owns no arrays: it reuses its base's
            v = f"A{f['px']}"
            w(fh, f"/* ---- {f['px']} px ---- */")
            w(fh, f"static const atlas_glyph_t {v}_GLYPHS[ATLAS_GLYPH_COUNT] = {{")
            for (off, gw, gh, adv, bx, by) in f["glyphs"]:
                w(fh, f"    {{ {off}u, {gw}, {gh}, {adv}, {bx}, {by} }},")
            w(fh, "};")
            if f["extra"]:
                w(fh)
                w(fh, f"static const atlas_extra_t {v}_EXTRA[{len(f['extra'])}] = {{")
                for (cp, off, gw, gh, adv, bx, by) in f["extra"]:
                    w(fh, f"    {{ {cp}u, {off}u, {gw}, {gh}, {adv}, {bx}, {by} }},")
                w(fh, "};")
            w(fh)
            blob = f["blob"]
            w(fh, f"static const uint8_t {v}_BITS[{max(len(blob), 1)}] = {{")
            for i in range(0, len(blob), 16):
                w(fh, "    " + " ".join(f"0x{b:02X}," for b in blob[i:i + 16]))
            w(fh, "};")
            w(fh)

        w(fh, "/* The ladder, in ascending size order. font_id_t indexes this table, so the")
        w(fh, " * enum and this array are both built from ATLAS_LADDER and cannot fall out of step.")
        w(fh, " *")
        w(fh, " * AN UPSCALED FACE POINTS AT ITS BASE'S ARRAYS. Its glyphs/bits/extra are the base")
        w(fh, " * face's, and its ascent/descent/line_height are the base's already multiplied by the")
        w(fh, " * factor; the accessors in fonts.c scale the per-glyph metrics at lookup. Nothing here")
        w(fh, " * is duplicated, which is the whole reason these sizes fit in the slot at all. */")
        w(fh, "static const atlas_face_t ATLAS_FACES[ATLAS_FACE_COUNT] = {")
        for f in faces:
            if f["upscale"] != 1:
                base = f"A{f['upscale_of']}"
                extra_ref = (f"{base}_EXTRA, {len(f['extra'])}" if f["extra"] else "NULL, 0")
                w(fh, f"    {{ {f['px']}, {f['ascent']}, {f['descent']}, {f['line_height']}, "
                      f"{base}_GLYPHS, {base}_BITS, {extra_ref}, {f['upscale']}, "
                      f"{f['upscale_of']} }},")
            else:
                v = f"A{f['px']}"
                extra_ref = f"{v}_EXTRA, {len(f['extra'])}" if f["extra"] else "NULL, 0"
                w(fh, f"    {{ {f['px']}, {f['ascent']}, {f['descent']}, "
                      f"{f['ascent'] + f['descent']}, {v}_GLYPHS, {v}_BITS, {extra_ref}, "
                      f"1, {f['px']} }},")
        w(fh, "};")

    total = sum(len(f["blob"]) for f in faces if f["upscale"] == 1)
    print(f"wrote {out_path}: {len(faces)} faces, {total} bitmap bytes "
          f"({total / 1024:.1f} KiB), ladder {ladder}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf-regular", required=True, help="face used below --bold-from")
    ap.add_argument("--ttf-bold", required=True, help="face used at and above --bold-from")
    ap.add_argument("--ladder", required=True,
                    help="comma-separated pixel sizes to RASTERISE, ascending, e.g. 16,20,24,64")
    ap.add_argument("--upscale", default="",
                    help="sizes drawn as a k x k BLOCK SCALE of a rasterised size, as "
                         "'px:base:k' triples, e.g. 256:128:2,512:128:4. They cost no bitmap "
                         "bytes: the face reuses its base's arrays and scales the metrics.")
    ap.add_argument("--bold-from", type=int, default=32,
                    help="smallest size that uses the bold face")
    ap.add_argument("--out", required=True,
                    help="the DATA header (atlas.h), included by fonts.c alone. The small "
                         "ladder-shape header is written beside it as atlas_ladder.h.")
    ap.add_argument("--ladder-out", default=None,
                    help="where to write the ladder-shape header. Defaults to beside --out, "
                         "but it must land on the INCLUDE path (lib/layout/include/) because "
                         "fonts.h includes it, while the data header stays private to fonts.c.")
    ap.add_argument("--extra", default="",
                    help="non-ASCII characters to add, e.g. '°' for the degree sign. "
                         "Appended after the ASCII glyphs so existing indices are unchanged.")
    args = ap.parse_args()

    sizes = [int(s) for s in args.ladder.replace(" ", "").split(",") if s]
    if not sizes:
        sys.exit("empty ladder")
    if sizes != sorted(sizes):
        sys.exit(f"ladder must be ascending: {sizes}")
    if len(set(sizes)) != len(sizes):
        sys.exit(f"duplicate size in ladder: {sizes}")

    # `by` is stored as int8_t, so a bearing below -128 would silently wrap to a POSITIVE
    # value and place the glyph BELOW the baseline — a defect that looks like a broken atlas,
    # not an overflow. The deepest ink at a given size is roughly -(px - descent), so this
    # bounds the RASTERISED ladder at a size where that cannot reach -128. Guarding here rather
    # than in the struct keeps the struct compact for every real face. (Upscaled faces are
    # immune: they store the BASE's bearings and the renderer multiplies at blit time, so no
    # int8_t ever holds a value past the ceiling.)
    for px in sizes:
        if px > 128:
            sys.exit(f"ladder size {px}px exceeds the 128px ceiling: the int8_t y bearing "
                     f"would overflow and silently wrap, placing glyphs under the baseline")

    faces = []
    base_of_px = {}
    for px in sizes:
        ttf = args.ttf_bold if px >= args.bold_from else args.ttf_regular
        f = build_face(ttf, px, args.extra)
        faces.append(f)
        base_of_px[px] = f

    # Upscaled sizes: `px:base:k`, each a k x block scale of a rasterised (or already-planned)
    # base. They add faces to the ladder but no bitmap bytes. Validated here rather than trusted,
    # because a mismatched product would emit a face whose metrics and blitter scale disagree —
    # text whose line box and glyph ink are drawn at two different sizes.
    for spec in (args.upscale or "").replace(" ", "").split(","):
        if not spec:
            continue
        try:
            px, base_px, k = (int(x) for x in spec.split(":"))
        except ValueError:
            sys.exit(f"bad --upscale entry {spec!r}: expected px:base:k")
        if base_px not in base_of_px:
            sys.exit(f"--upscale {spec}: base {base_px}px is not a rasterised ladder size")
        if k < 2:
            sys.exit(f"--upscale {spec}: factor must be >= 2 (1x would duplicate the base)")
        if px != base_px * k:
            sys.exit(f"--upscale {spec}: px must equal base * k ({base_px * k})")
        if px in base_of_px:
            sys.exit(f"--upscale {spec}: {px}px is already a rasterised ladder size")
        faces.append(build_upscaled(px, base_of_px[base_px], k))
        base_of_px[px] = faces[-1]

    faces.sort(key=lambda f: f["px"])
    if len({f["px"] for f in faces}) != len(faces):
        sys.exit("duplicate size after applying --upscale")

    ladder_path = args.ladder_out or os.path.join(os.path.dirname(args.out), "atlas_ladder.h")
    emit_ladder(ladder_path, faces)
    emit_data(args.out, faces, args.bold_from)
    return 0


if __name__ == "__main__":
    sys.exit(main())
