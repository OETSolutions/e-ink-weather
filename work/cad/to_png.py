"""Rasterize projected polylines (JSON from dump_views.py) into a simple SVG + PNG sheet."""
import json, sys, os
import numpy as np

src = sys.argv[1]
outpng = sys.argv[2]
views = json.load(open(src))

ORDER = ["front", "side", "iso", "iso2", "top"]
W = 1400
PAD = 16


def rasterize(polys, w, colour=0, ss=2):
    xs = [p[0] for pl in polys for p in pl]
    ys = [p[1] for pl in polys for p in pl]
    if not xs:
        return None
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    sc = (w - 2 * PAD) / max(x1 - x0, 1e-9)
    h = int((y1 - y0) * sc + 2 * PAD)
    H = h * ss
    img = np.full((H, w * ss), 255, dtype=np.uint8)

    def put(px, py):
        ix, iy = int(px), int(py)
        if 0 <= iy < H and 0 <= ix < w * ss:
            img[iy, ix] = colour

    for pl in polys:
        for i in range(len(pl) - 1):
            ax = PAD + (pl[i][0] - x0) * sc
            ay = PAD + (pl[i][1] - y0) * sc
            bx = PAD + (pl[i + 1][0] - x0) * sc
            by = PAD + (pl[i + 1][1] - y0) * sc
            n = int(max(abs(bx - ax), abs(by - ay))) + 1
            for t in np.linspace(0, 1, n):
                put((ax + (bx - ax) * t) * ss, (ay + (by - ay) * t) * ss)
    # downsample
    img = img[::ss, ::ss]
    return img


sheets = []
for name in ORDER:
    if name not in views:
        continue
    im = rasterize(views[name], W)
    if im is None:
        continue
    sheets.append((name, im))
    print("%-6s -> %dx%d" % (name, im.shape[1], im.shape[0]))

total_h = sum(im.shape[0] + 6 for _, im in sheets)
canvas = np.full((total_h, W, 3), 255, dtype=np.uint8)
y = 0
for name, im in sheets:
    canvas[y:y + im.shape[0], :im.shape[1], :] = im[:, :, None]
    canvas[y:y + 2, :, :] = 0     # separator
    y += im.shape[0] + 6

from struct import pack
h, w2, _ = canvas.shape
with open(outpng, "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (w2, h))
    f.write(canvas.tobytes())
print("wrote", outpng, "%dx%d" % (w2, h))
