#!/usr/bin/env python3
"""Dump a raw 1 bpp framebuffer to a PNG so a human can actually LOOK at it.

Bit convention (HW-6): 1 = white, 0 = black, MSB-first, row-major, 920x680.

Used to eyeball a golden image before locking it (NFR-9) — a byte count tells you nothing
about whether the text landed on the baseline.
"""
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

WIDTH, HEIGHT = 920, 680
PITCH = WIDTH // 8
BYTES = PITCH * HEIGHT


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <fb.bin> <out.png>")
    data = open(sys.argv[1], "rb").read()
    if len(data) != BYTES:
        sys.exit(f"{sys.argv[1]}: expected {BYTES} bytes, got {len(data)}")

    img = Image.new("1", (WIDTH, HEIGHT))
    px = img.load()
    for y in range(HEIGHT):
        row = y * PITCH
        for x in range(WIDTH):
            # 1 = white, 0 = black.
            white = (data[row + (x >> 3)] >> (7 - (x & 7))) & 1
            px[x, y] = 255 if white else 0
    img.save(sys.argv[2])
    print(f"wrote {sys.argv[2]} ({WIDTH}x{HEIGHT})")


if __name__ == "__main__":
    main()
