#!/usr/bin/env python3
"""Generate the boot splash from the OETSolutions / Aether logo.

WHY THIS IS A GENERATOR AND NOT A COMMITTED BINARY: the source is a 1254x1254 RGBA PNG with
large transparent margins. Committing the derived 1bpp array alone would mean nobody could see
where it came from or regenerate it if the artwork changes; committing the source would put a
1 MB file in the repo for one asset. The generator keeps both out: point it at the source PNG
and it reproduces the asset, and the committed output is only the bytes the panel needs.

It trims the transparent dead space, scales the artwork to sit centred on the panel with a
margin, and thresholds to 1 bit. The threshold is deliberately near the middle of the observed
ink luminance range (52..111) and is not gamma-corrected: the art is dark grey and the panel is
1 bit, so anything between the floor and the ink reads as black and the edge stays smooth.

Run from firmware/:  python3 tools/gen_boot_logo.py <logo.png>
Output:              src/boot_logo_data.c
"""

import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: python3 -m pip install Pillow")

PANEL_W, PANEL_H = 920, 680
MARGIN = 70          # keeps the full-page mark clear of the bezel edge
THRESHOLD = 190      # alpha-normalised: below this is ink. See the note above.

# The SMALL badge, for the provisioning screen's header. Sized to the height the layout gives
# it rather than to the page: a full-panel-height mark shrunk into a header strip is illegible.
BADGE_H = 80
BADGE_MARGIN = 0     # the badge is its own image, so it needs no page margin

DEFAULT_SRC = ("/Users/cbrown/Library/CloudStorage/GoogleDrive-cbrown350@gmail.com/"
               "My Drive/oetsolutions.com/OETSolutions-Aether Combined Logo.png")


def pack(grey, w, h):
    """Pack an 8-bit image into 1bpp, MSB-first, row-major, bit SET = WHITE (HW-6)."""
    pitch = (w + 7) // 8
    out = bytearray(pitch * h)
    px = grey.load()
    for y in range(h):
        row = y * pitch
        for x in range(w):
            if px[x, y] < THRESHOLD:
                out[row + (x >> 3)] &= ~(0x80 >> (x & 7))   # clear bit = black ink
            else:
                out[row + (x >> 3)] |= (0x80 >> (x & 7))
    return bytes(out)


def count_ink(buf, w):
    """Ink = a CLEAR bit (black), so count the zeros."""
    pitch = (w + 7) // 8
    return sum(8 - bin(b).count("1") for b in buf)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
    im = Image.open(src).convert("RGBA")

    # ---- Trim the transparent dead space ----
    # The source is 1254x1254 with the artwork occupying a thin horizontal band, so without
    # this the logo would render tiny in the middle of a mostly-empty frame.
    #
    # The bbox is computed from alpha > 32 rather than Image.getbbox(): getbbox() counts ANY
    # non-zero alpha, and this artwork has faint anti-aliased haze hundreds of pixels into the
    # margins. Trusting getbbox() left the trim almost as tall as the original (1191 of 1254),
    # so the logo then scaled down to fit that phantom height and rendered far smaller than it
    # should. Measured content is a 679-row band, not 1191.
    alpha = im.split()[3]
    boxes = ((alpha.point(lambda v: 255 if v > 32 else 0)).getbbox())
    if boxes is None:
        sys.exit("the source image is fully transparent")
    im = im.crop(boxes)
    print(f"trimmed to {im.size} (was {Image.open(src).size})")

    # ---- Scale to fit inside the panel, preserving aspect ratio ----
    avail_w = PANEL_W - 2 * MARGIN
    avail_h = PANEL_H - 2 * MARGIN
    scale = min(avail_w / im.width, avail_h / im.height)
    new_w = max(1, int(im.width * scale))
    new_h = max(1, int(im.height * scale))
    im = im.resize((new_w, new_h), Image.LANCZOS)
    print(f"scaled to {im.size} (fit factor {scale:.3f})")

    # ---- Composite onto white and threshold ----
    # White page, not black: the panel's resting state is white and the splash should look like
    # a printed mark, not an inverted plate.
    page = Image.new("RGBA", (PANEL_W, PANEL_H), (255, 255, 255, 255))
    page.alpha_composite(im, ((PANEL_W - new_w) // 2, (PANEL_H - new_h) // 2))
    grey = page.convert("L")

    # 1bpp, MSB-first, row-major, bit SET = white — the framebuffer convention (HW-6).
    out = pack(grey, PANEL_W, PANEL_H)
    total = PANEL_W * PANEL_H
    ink = count_ink(out, PANEL_W)
    print(f"splash: ink coverage {ink / total:.1%} of the panel")

    # ---- The small badge, for the provisioning screen header ----
    # Same artwork, scaled to a strip height instead of the page. Emitted as a standalone image
    # so the caller blits it at a width it can measure rather than cropping the full frame.
    bh = BADGE_H
    bw = max(1, int(im.width * (bh / im.height)))
    badge_img = im.resize((bw, bh), Image.LANCZOS)
    bpage = Image.new("RGBA", (bw, bh), (255, 255, 255, 255))
    bpage.alpha_composite(badge_img, (0, 0))
    badge = pack(bpage.convert("L"), bw, bh)
    print(f"badge: {bw}x{bh}, {len(badge)} bytes")

    # ---- Emit ----
    lines = [
        "/* GENERATED by tools/gen_boot_logo.py — do not edit by hand.",
        " *",
        " * The OETSolutions / Aether mark, trimmed, scaled and thresholded to 1bpp in the",
        " * panel's framebuffer convention (MSB-first, row-major, bit SET = white). See the",
        " * generator for the pipeline and why the asset is derived rather than committed raw.",
        " *",
        " * TWO SIZES from one source: `boot_logo` fills the panel for the splash, `boot_badge`",
        " * is a header strip for the provisioning screen. Generating both here keeps them from",
        " * drifting apart if the artwork changes.",
        " */",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        f"const uint8_t boot_logo[{len(out)}] = {{",
    ]
    for i in range(0, len(out), 16):
        lines.append("    " + ",".join("0x%02X" % b for b in out[i:i + 16]) + ",")
    lines.append("};")
    lines.append("const size_t boot_logo_len = sizeof(boot_logo);")
    lines.append("")
    lines.append(f"/* {bw}x{bh} header badge, blitted at its natural size. */")
    lines.append(f"const int boot_badge_w = {bw};")
    lines.append(f"const int boot_badge_h = {bh};")
    lines.append(f"const uint8_t boot_badge[{len(badge)}] = {{")
    for i in range(0, len(badge), 16):
        lines.append("    " + ",".join("0x%02X" % b for b in badge[i:i + 16]) + ",")
    lines.append("};")
    lines.append("const size_t boot_badge_len = sizeof(boot_badge);")
    lines.append("")

    with open("src/boot_logo_data.c", "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote src/boot_logo_data.c")


if __name__ == "__main__":
    main()
