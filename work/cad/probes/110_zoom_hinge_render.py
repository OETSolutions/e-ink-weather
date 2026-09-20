"""Zoom render of the hinge region only.

render3d.py auto-fits the whole bounding box, so a 3 mm clip is a few pixels. This
crops the tessellation to a YZ window around one hinge station and renders THAT, so
the clip mouth, the cavity walls, the pin and the gusset can actually be judged.

Env: SRC, OUT, CX (clip station X), plus the same AZ/EL/W/PERSP as render3d.
"""
import FreeCAD as App
import Part
import numpy as np
import os

SRC = os.environ["SRC"]
OUT = os.environ["OUT"]
CX = float(os.environ.get("CX", "67.2"))
AZ = [float(v) for v in os.environ.get("AZ", "0,0,40").split(",")]
EL = [float(v) for v in os.environ.get("EL", "0,0,25").split(",")]
W = int(os.environ.get("W", "760"))
YR = float(os.environ.get("YR", "8.0"))    # half-window in Y about the hinge axis
ZR = float(os.environ.get("ZR", "6.0"))    # half-window in Z about the hinge axis
WX = float(os.environ.get("WX", "5.0"))    # half-window in X about the clip station
PARTS = [s for s in os.environ.get("PARTS", "").split(",") if s]
HINGE_Y = float(os.environ.get("HY", "5.0"))
KNUCKLE_Z = float(os.environ.get("KZ", "-0.68"))

PART_COLOUR = {
    "PRINT_REAR_COVER": (0.72, 0.75, 0.80),
    "PRINT_FOOT": (0.95, 0.55, 0.20),
    "PRINT_SERVICE_HATCH": (0.90, 0.40, 0.60),
}

doc = App.openDocument(SRC)
T = []
C = []
# CUT the solids to the window (not filter triangles by centroid -- a huge cover face whose
# centroid happens to land in the box would otherwise be kept whole and swamp the view).
win = Part.makeBox(2 * WX, 2 * YR, 2 * ZR,
                   App.Vector(CX - WX, HINGE_Y - YR, KNUCKLE_Z - ZR))
for o in doc.Objects:
    if o.Name not in PART_COLOUR:
        continue
    if PARTS and o.Name not in PARTS:
        continue
    if not hasattr(o, "Shape") or o.Shape.isNull():
        continue
    shape = o.Shape.common(win)
    verts, facets = shape.tessellate(0.10)
    V = np.array([[p.x, p.y, p.z] for p in verts], dtype=np.float64)
    F = np.array(facets, dtype=np.int32)
    if len(F) == 0:
        continue
    tri = V[F]
    if len(tri) == 0:
        continue
    T.append(tri)
    C.append(np.tile(np.array(PART_COLOUR[o.Name]), (len(tri), 1)))
T = np.concatenate(T, axis=0)
C = np.concatenate(C, axis=0)
print("triangles in window:", len(T))

lo = T.reshape(-1, 3).min(axis=0)
hi = T.reshape(-1, 3).max(axis=0)
ctr = (lo + hi) * 0.5
radius = float(np.linalg.norm(hi - lo)) * 0.5

e1 = T[:, 1] - T[:, 0]
e2 = T[:, 2] - T[:, 0]
N = np.cross(e1, e2)
ar = np.linalg.norm(N, axis=1)
good = ar > 1e-12
N[good] /= ar[good][:, None]
cen = T.mean(axis=1)

LIGHTS = [(np.array([0.45, -0.75, 0.50]), 0.62),
          (np.array([-0.60, -0.35, 0.30]), 0.25),
          (np.array([0.0, 0.0, -1.0]), 0.13)]
LIGHTS = [(d / np.linalg.norm(d), w) for d, w in LIGHTS]


def basis(az, el):
    a, e = np.radians(az), np.radians(el)
    fwd = np.array([np.cos(e) * np.cos(a), np.cos(e) * np.sin(a), np.sin(e)])
    up0 = np.array([0.0, 0.0, 1.0])
    if abs(abs(e) - 90.0) < 0.5:
        up0 = np.array([0.0, 1.0, 0.0])
    right = np.cross(fwd, up0)
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    return fwd, right, up


def render(az, el, w=W):
    fwd, right, up = basis(az, el)
    P = T.reshape(-1, 3) - ctr
    X = (P @ right).reshape(-1, 3)
    Y = (P @ up).reshape(-1, 3)
    Z = (P @ fwd).reshape(-1, 3)
    x0, x1 = X.min(), X.max()
    y0, y1 = Y.min(), Y.max()
    scale = (w - 12) / max(x1 - x0, 1e-6)
    h = max(int((y1 - y0) * scale) + 12, 8)
    img = np.zeros((h, w, 3), dtype=np.float32)
    zbuf = np.full((h, w), 1e30, dtype=np.float32)
    Nv_z = N @ fwd
    shade = np.zeros(len(T), dtype=np.float32)
    for d, wt in LIGHTS:
        shade += wt * np.clip(N @ d, 0.0, 1.0)
    shade = np.clip(0.20 + 0.80 * shade, 0.0, 1.35)
    px = (X - x0) * scale + 6.0
    py = (y1 - Y) * scale + 6.0
    front = Nv_z < 0.0
    order = np.argsort(cen @ fwd)[::-1]
    for i in order:
        if not front[i]:
            continue
        a = np.array([px[i, 0], py[i, 0]])
        b = np.array([px[i, 1], py[i, 1]])
        c = np.array([px[i, 2], py[i, 2]])
        minx = max(int(np.floor(min(a[0], b[0], c[0]))), 0)
        maxx = min(int(np.ceil(max(a[0], b[0], c[0]))) + 1, w - 1)
        miny = max(int(np.floor(min(a[1], b[1], c[1]))), 0)
        maxy = min(int(np.ceil(max(a[1], b[1], c[1]))) + 1, h - 1)
        if minx > maxx or miny > maxy:
            continue
        xs = np.arange(minx, maxx + 1) + 0.5
        ys = np.arange(miny, maxy + 1) + 0.5
        gx, gy = np.meshgrid(xs, ys)
        v0 = b - a
        v1 = c - a
        den = v0[0] * v1[1] - v1[0] * v0[1]
        if abs(den) < 1e-12:
            continue
        v2x = gx - a[0]
        v2y = gy - a[1]
        u = (v2x * v1[1] - v1[0] * v2y) / den
        vv = (v0[0] * v2y - v2x * v0[1]) / den
        m = (u >= -1e-6) & (vv >= -1e-6) & (u + vv <= 1.0 + 1e-6)
        if not m.any():
            continue
        zp = Z[i, 0] + u * (Z[i, 1] - Z[i, 0]) + vv * (Z[i, 2] - Z[i, 0])
        sub = zbuf[miny:maxy + 1, minx:maxx + 1]
        better = m & (zp < sub)
        if not better.any():
            continue
        sub[better] = zp[better]
        img[miny:maxy + 1, minx:maxx + 1][better] = np.clip(C[i] * shade[i], 0, 1)
    return img


views = [render(az, el) for az, el in zip(AZ, EL)]
GAP = 8
total_h = sum(v.shape[0] + GAP for v in views)
canvas = np.full((total_h, W, 3), 1.0, dtype=np.float32)
y = 0
for v in views:
    canvas[y:y + v.shape[0], :v.shape[1]] = v
    y += v.shape[0] + GAP
    canvas[y - 4:y - 2, :, :] = 0.0
a = (canvas * 255).astype(np.uint8)
with open(OUT, "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (a.shape[1], a.shape[0]))
    f.write(a.tobytes())
print("wrote", OUT, "%dx%d" % (a.shape[1], a.shape[0]))
