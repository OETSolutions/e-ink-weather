#!/usr/bin/env python3
"""Generate the weather icon set as a 1 bpp C array.

WHY A GENERATOR AND NOT HAND-DRAWN BYTES: a weather icon is a small drawing, and a hand-packed
bitmap is unreviewable and unmaintainable — nobody can tell a correct snowflake from a wrong one
in a hex dump. Drawing from primitives keeps the geometry readable and editable, and the output
is only the bytes the panel needs.

WHY THE FIRMWARE DRAWS THE ICON RATHER THAN THE WEB APP BAKING IT INTO THE STATIC LAYER: the
icon describes the CURRENT CONDITIONS, so it changes between refreshes — clear one hour, rain the
next. A baked icon would freeze whatever the weather was when the layout was uploaded. FR-1 lists
"icons" as part of the static layer, but also requires the renderer boundary to stay open "to do
on-device easily"; a condition icon is the case where on-device is the only correct answer.

CONVENTIONS, both load-bearing:
  - A SET bit is INK (the atlas convention, not the framebuffer's). The blitter inverts, so an
    icon can be stamped onto the static layer like a glyph and paint ONLY ink — a white box
    around it would punch a hole through the layout's chrome.
  - Drawn at SUPERSAMPLE times the final size, then thresholded down. A 1-bit panel cannot
    anti-alias, so the edge quality comes from the downscale rather than from dithering, which
    would make the icon look dirty.

Run from firmware/:  python3 tools/gen_weather_icons.py
Output:              lib/layout/src/weather_icons_data.c
Preview (LOOK AT IT): python3 tools/gen_weather_icons.py --preview /tmp/icons.png
"""

import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("Pillow is required: python3 -m pip install Pillow")

# 64 px at 198 dpi is ~8 mm — legible across a room, and the same visual weight as the body face
# so an icon beside a condition word reads as a pair rather than as two unrelated marks.
ICON_PX = 64
SS = 4                      # supersample factor for the downscale
BLACK, WHITE = 0, 255

# The order here IS the firmware's index space (weather_icon_index). Appending is safe; inserting
# or reordering renumbers the icons and the firmware's mapping must change with it.
ICONS = [
    "clear",        # 0  OWM 01d
    "clear_night",  # 1  OWM 01n
    "partly",       # 2  OWM 02d
    "partly_night", # 3  OWM 02n
    "cloudy",       # 4  OWM 03d/04d
    "rain",         # 5  OWM 09d/10d
    "storm",        # 6  OWM 11d
    "snow",         # 7  OWM 13d
    "fog",          # 8  OWM 50d
]


def S(v):
    """Scale a 0..1 fraction of the icon to supersampled pixels."""
    return int(round(v * ICON_PX * SS))


def circle(d, cx, cy, r, fill=BLACK):
    d.ellipse([S(cx) - S(r), S(cy) - S(r), S(cx) + S(r), S(cy) + S(r)], fill=fill)


def rays(d, cx, cy, r0, r1, width):
    """Eight sun rays, drawn as radiating bars. Diagonal ones use a polygon so they have the
    same visual weight as the orthogonal ones (a line of the same width looks thinner at 45)."""
    import math
    for k in range(8):
        a = k * math.pi / 4.0
        x0, y0 = cx + r0 * math.cos(a), cy + r0 * math.sin(a)
        x1, y1 = cx + r1 * math.cos(a), cy + r1 * math.sin(a)
        d.line([S(x0), S(y0), S(x1), S(y1)], fill=BLACK, width=S(width))


def sun(d, cx, cy, r, with_rays=True):
    if with_rays:
        rays(d, cx, cy, r * 1.28, r * 1.85, 0.055)
    circle(d, cx, cy, r)


def moon(d, cx, cy, r):
    """A crescent: a filled disc with an offset disc cut back out of it. Drawn as a single black
    crescent rather than an outline, so it reads at a glance like the sun does."""
    circle(d, cx, cy, r)
    circle(d, cx + r * 0.62, cy - r * 0.28, r * 0.92, fill=WHITE)


def cloud(d, cx, cy, w, h):
    """Three overlapping lobes on a flat base — the standard cloud silhouette."""
    r = h * 0.5
    circle(d, cx - w * 0.28, cy + h * 0.10, r * 0.92)
    circle(d, cx, cy - h * 0.12, r * 1.12)
    circle(d, cx + w * 0.30, cy + h * 0.10, r * 0.86)
    d.rectangle([S(cx - w * 0.30), S(cy + h * 0.10), S(cx + w * 0.30), S(cy + h * 0.52)], fill=BLACK)


def rain(d, cx, cy, n=3):
    """Short slanted strokes under the cloud — slanted so they read as falling rain and not as a
    fence, which is what vertical strokes look like at this size."""
    for i in range(n):
        x = cx + (i - (n - 1) / 2.0) * 0.30
        d.line([S(x + 0.10), S(cy), S(x - 0.02), S(cy + 0.24)], fill=BLACK, width=S(0.052))


def bolt(d, cx, cy):
    d.polygon([(S(cx + 0.04), S(cy)),
               (S(cx - 0.14), S(cy + 0.19)),
               (S(cx + 0.00), S(cy + 0.19)),
               (S(cx - 0.09), S(cy + 0.40)),
               (S(cx + 0.17), S(cy + 0.13)),
               (S(cx + 0.02), S(cy + 0.13))], fill=BLACK)


def flakes(d, cx, cy, n=3):
    """Asterisk snowflakes: three strokes crossed through a point."""
    import math
    for i in range(n):
        x = cx + (i - (n - 1) / 2.0) * 0.32
        y = cy + 0.14
        rr = 0.075
        for k in range(3):
            a = k * math.pi / 3.0
            d.line([S(x - rr * math.cos(a)), S(y - rr * math.sin(a)),
                    S(x + rr * math.cos(a)), S(y + rr * math.sin(a))],
                   fill=BLACK, width=S(0.030))


def bars(d, cy, n=3):
    """Fog: stacked horizontal bars of equal weight. Unequal bars read as a broken ladder rather
    than as mist, so the width tapers only slightly and every bar keeps the same thickness."""
    for i in range(n):
        y = cy + (i - (n - 1) / 2.0) * 0.19
        half = 0.42 - abs(i - (n - 1) / 2.0) * 0.04
        d.rectangle([S(0.5 - half), S(y - 0.048), S(0.5 + half), S(y + 0.048)], fill=BLACK)


def partly_symbol(d, cx, cy, r, night=False):
    """The disc peeking out from behind the cloud for a partially-cloudy icon.

    NO RAYS, DELIBERATELY. At 64 px a ray that is not fully clear of the cloud downscales to a
    1-2 px speck floating beside the cloud — which reads as dirt on the panel, not as a sun. A
    plain disc, half-hidden behind the cloud, is unambiguous and is how the icon is usually
    drawn at small sizes. The rays belong on the 'clear' icon, where nothing occludes them.
    """
    if night:
        moon(d, cx, cy, r)
    else:
        circle(d, cx, cy, r)


def draw_icon(name, d):
    if name == "clear":
        sun(d, 0.5, 0.5, 0.19)
    elif name == "clear_night":
        moon(d, 0.5, 0.5, 0.22)
    elif name == "partly":
        partly_symbol(d, 0.36, 0.35, 0.185)
        cloud(d, 0.57, 0.63, 0.60, 0.38)
    elif name == "partly_night":
        partly_symbol(d, 0.36, 0.35, 0.185, night=True)
        cloud(d, 0.57, 0.63, 0.60, 0.38)
    elif name == "cloudy":
        cloud(d, 0.5, 0.48, 0.78, 0.50)
    elif name == "rain":
        cloud(d, 0.5, 0.40, 0.74, 0.44)
        rain(d, 0.5, 0.72, 3)
    elif name == "storm":
        cloud(d, 0.5, 0.38, 0.72, 0.42)
        bolt(d, 0.5, 0.66)
    elif name == "snow":
        cloud(d, 0.5, 0.40, 0.74, 0.44)
        flakes(d, 0.5, 0.68, 3)
    elif name == "fog":
        bars(d, 0.5, 3)
    else:
        raise SystemExit(f"no drawing for icon {name!r}")


def render(name):
    """Draw one icon at supersample, downscale, and return a 1bpp packed buffer (SET = ink)."""
    big = Image.new("L", (ICON_PX * SS, ICON_PX * SS), WHITE)
    draw_icon(name, ImageDraw.Draw(big))
    small = big.resize((ICON_PX, ICON_PX), Image.LANCZOS)

    pitch = (ICON_PX + 7) // 8
    out = bytearray(pitch * ICON_PX)
    px = small.load()
    for y in range(ICON_PX):
        row = y * pitch
        for x in range(ICON_PX):
            # THRESHOLD AT MID-GREY, not near-white: the downscale produces a grey ramp along
            # every edge, and a high threshold would thicken the icon into a blob while a low one
            # would erase the thin strokes (the rays, the snowflakes) entirely.
            if px[x, y] < 128:
                out[row + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def preview(path):
    """Render every icon side by side so the SET can be LOOKED at before it is flashed. The
    project's own rule: a generated asset is not verified until a human has seen it."""
    cols = len(ICONS)
    pad = 8
    canvas = Image.new("L", (cols * (ICON_PX + pad) + pad, ICON_PX + 2 * pad), WHITE)
    for i, name in enumerate(ICONS):
        buf = render(name)
        pitch = (ICON_PX + 7) // 8
        tile = Image.new("L", (ICON_PX, ICON_PX), WHITE)
        tp = tile.load()
        for y in range(ICON_PX):
            for x in range(ICON_PX):
                if buf[y * pitch + (x >> 3)] & (0x80 >> (x & 7)):
                    tp[x, y] = BLACK
        canvas.paste(tile, (pad + i * (ICON_PX + pad), pad))
    canvas.save(path)
    print(f"preview -> {path} ({', '.join(ICONS)})")


def emit_ts(blobs, out_path):
    """Emit the same icons as a TypeScript module for the web app's preview.

    WHY GENERATED FROM HERE RATHER THAN RE-DRAWN IN TS: the config app shows a live preview of
    the panel and the firmware draws the real icon with different code (NFR-4). Two independent
    drawings would be two chances to disagree, and the golden-image test could not catch it for
    an icon that is not in the default layout. Converting the SAME bits, exactly as
    gen_web_atlas.py does for the fonts, is what keeps the preview honest.

    The bit convention is copied AS-IS (a SET bit is ink); the inversion to the framebuffer's
    "clear = black" happens in the blitter, one place, same as the atlas."""
    lines = [
        "/* GENERATED by firmware/tools/gen_weather_icons.py — DO NOT EDIT.",
        " *",
        " * The firmware's weather icon set, as TypeScript, copied byte-for-byte so the preview",
        " * cannot disagree with the panel (NFR-4). See the generator for why these are converted",
        " * rather than drawn twice.",
        " *",
        " * Atlas convention: a SET bit is INK. The renderer inverts to the framebuffer's",
        " * clear-is-black convention when it blits.",
        " */",
        "",
        "export interface WeatherIcon {",
        "  w: number;",
        "  h: number;",
        "  /** 1 bpp, MSB-first, row-major, pitch = (w + 7) / 8, SET bit = ink. */",
        "  bits: Uint8Array;",
        "}",
        "",
        f"export const WEATHER_ICON_PX = {ICON_PX};",
        "",
        "export const WEATHER_ICONS: WeatherIcon[] = [",
    ]
    for name, buf in blobs:
        arr = ",".join(str(b) for b in buf)
        lines.append(f"  {{ w: {ICON_PX}, h: {ICON_PX}, bits: new Uint8Array([{arr}]) }},")
    lines.append("];")
    lines.append("")
    with open(out_path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {out_path}")


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--preview":
        preview(sys.argv[2])
        return 0

    blobs = [(n, render(n)) for n in ICONS]

    lines = [
        "/* GENERATED by tools/gen_weather_icons.py — do not edit by hand.",
        " *",
        f" * {len(ICONS)} weather condition icons, {ICON_PX}x{ICON_PX}, 1 bpp, MSB-first,",
        " * row-major, pitch = (w + 7) / 8, and A SET BIT IS INK (the atlas convention, so the",
        " * icon blits transparently over the static layer like a glyph).",
        " *",
        " * The index order matches OWM_ICON_* in icons.h and the mapping in weather_icon_index(),",
        " * which is host-tested. See the generator for why these are drawn rather than committed",
        " * as raw bytes, and why the firmware draws them instead of the web app baking them in.",
        " */",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "#include \"weather_icons.h\"",
        "",
    ]
    for i, (name, buf) in enumerate(blobs):
        lines.append(f"/* {i}: {name} */")
        lines.append(f"static const uint8_t icon_{name}[{len(buf)}] = {{")
        for j in range(0, len(buf), 16):
            lines.append("    " + ",".join("0x%02X" % b for b in buf[j:j + 16]) + ",")
        lines.append("};")
        lines.append("")

    lines.append("const weather_icon_t weather_icons[WEATHER_ICON_COUNT] = {")
    for i, (name, buf) in enumerate(blobs):
        lines.append(f"    {{ {ICON_PX}, {ICON_PX}, icon_{name} }},   /* {i}: {name} */")
    lines.append("};")
    lines.append("")

    with open("lib/layout/src/weather_icons_data.c", "w") as fh:
        fh.write("\n".join(lines) + "\n")
    total = sum(len(b) for _, b in blobs)
    print(f"wrote lib/layout/src/weather_icons_data.c ({len(ICONS)} icons, {total} bytes)")

    # The web app's copy, so its preview draws the same icons (NFR-4).
    emit_ts(blobs, "../webapp/src/canvas/weather-icons-data.ts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
