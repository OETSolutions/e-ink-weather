"""To-scale XY cross-section through the USB slot and the bay, at several Z heights.

An XY slice is the only view that answers "is the hole actually open". It rasterises the
cover and the hatch at a given Z, so a blocked passage shows as material and an open one
as blank.

Env: SRC, ZS (comma list of Z heights), RSCALE (px/mm)
"""
import os

import FreeCAD as App
import numpy as np

SRC = os.environ.get("SRC", "rear_cover_foot.FCStd")
ZS = [float(v) for v in os.environ.get("ZS", "2.0,0.0,-0.6").split(",")]
RSCALE = int(os.environ.get("RSCALE", "9"))
OUT = os.environ.get("OUT", "")

doc = App.openDocument(SRC)
cover = doc.getObject("PRINT_REAR_COVER").Shape
hatch = doc.getObject("PRINT_SERVICE_HATCH").Shape

# window: the whole service bay region
x0, x1 = 12.0, 96.0
y0, y1 = 62.0, 102.0
W = int((x1 - x0) * RSCALE)
H = int((y1 - y0) * RSCALE)


def mask(shape, z):
    wires = shape.slice(App.Vector(0, 0, 1), z)
    xx = np.linspace(x0, x1, W, endpoint=False) + (x1 - x0) / (2 * W)
    yy = np.linspace(y0, y1, H, endpoint=False) + (y1 - y0) / (2 * H)
    GX, GY = np.meshgrid(xx, yy)
    m = np.zeros((H, W), dtype=bool)
    for w in wires:
        lp = []
        for e in w.OrderedEdges:
            for p in e.discretize(30):
                lp.append((p.x, p.y))
        for i in range(len(lp)):
            ax, ay = lp[i]
            bx, by = lp[(i + 1) % len(lp)]
            if ay == by:
                continue
            cond = (ay > GY) != (by > GY)
            xint = (bx - ax) * (GY - ay) / (by - ay) + ax
            m ^= cond & (GX < xint)
    return m


imgs = []
for z in ZS:
    img = np.full((H, W, 3), 1.0, dtype=np.float32)
    mc = mask(cover, z)
    mh = mask(hatch, z)
    img[mc] = (0.45, 0.75, 0.50)
    img[mh & ~mc] = (0.85, 0.45, 0.60)
    img[mc & mh] = (0.95, 0.1, 0.1)
    print("z=%.2f  cover %.3f mm2  hatch %.3f mm2  overlap %.3f mm2"
          % (z, mc.sum() / RSCALE**2, mh.sum() / RSCALE**2, (mc & mh).sum() / RSCALE**2))
    a = (img * 255).astype(np.uint8)
    with open("/tmp/hsec_%+.1f.ppm" % z, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        f.write(a.tobytes())
    imgs.append(img)

if OUT:
    gap = np.ones((6, W, 3), dtype=np.float32)
    rows = []
    for im in imgs:
        rows.append(im)
        rows.append(gap)
    canvas = np.concatenate(rows, axis=0)
    a = (canvas * 255).astype(np.uint8)
    with open(OUT, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (a.shape[1], a.shape[0]))
        f.write(a.tobytes())
    print("wrote", OUT, "%dx%d  window x %.1f..%.1f y %.1f..%.1f"
          % (a.shape[1], a.shape[0], x0, x1, y0, y1))
