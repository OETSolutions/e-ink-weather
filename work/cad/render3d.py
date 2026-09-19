"""Shaded software render of the assembly (numpy z-buffer, no external deps).

The previous wireframe views could not show what the user actually sees. This renders
solid shaded surfaces with per-part colours so print-part interference, exposed holes,
and surface features are visible.

Env:
  SRC   .FCStd to open
  OUT   .ppm output
  OBJS  optional comma-separated object names to include (default: all with a Shape)
  AZ    comma-separated azimuths in degrees          (default: 35,-40,0,90,180)
  EL    comma-separated elevations in degrees        (default: 25,25,85,15,15)
  W     image width in px                            (default: 520)
  PERSP optional "1" for perspective instead of ortho
"""
import FreeCAD as App
import numpy as np
import os, sys, json

SRC = os.environ["SRC"]
OUT = os.environ["OUT"]
AZ = [float(v) for v in os.environ.get("AZ", "35,-40,0,90,180").split(",")]
EL = [float(v) for v in os.environ.get("EL", "25,25,85,15,15").split(",")]
W = int(os.environ.get("W", "520"))
OBJS = [s for s in os.environ.get("OBJS", "").split(",") if s]
PERSP = os.environ.get("PERSP") == "1"

PALETTE = [
    (0.82, 0.84, 0.88),   # light grey  - printed case
    (0.55, 0.68, 0.85),   # blue        - bezel
    (0.90, 0.72, 0.42),   # amber       - chassis
    (0.62, 0.82, 0.62),   # green       - cover/foot
    (0.85, 0.55, 0.55),   # red         - board
    (0.45, 0.45, 0.50),   # dark grey   - metal
    (0.20, 0.20, 0.24),   # near-black  - panel glass
    (0.95, 0.85, 0.95),   # pink        - battery
    (0.70, 0.60, 0.90),   # violet      - misc
]

GROUP_COLOUR = {
    "PRINTABLE_CASE_PARTS": (0.80, 0.82, 0.86),
    "DISPLAY_ASSEMBLY": (0.25, 0.28, 0.34),
    "ESP32_M1_ASSEMBLY": (0.30, 0.55, 0.35),
    "POWER_ASSEMBLY": (0.85, 0.70, 0.30),
    "HARDWARE_REFERENCE": (0.40, 0.40, 0.45),
}
# Per-part colours inside PRINTABLE_CASE_PARTS so the five prints are distinguishable.
PART_COLOUR = {
    "PRINT_FRONT_BEZEL": (0.35, 0.60, 0.90),
    "PRINT_REAR_CHASSIS": (0.90, 0.72, 0.38),
    "PRINT_REAR_COVER": (0.45, 0.80, 0.50),
    "PRINT_SERVICE_HATCH": (0.90, 0.45, 0.60),
    "PRINT_SWING_FOOT": (0.65, 0.50, 0.90),
}

doc = App.openDocument(SRC)
items = []
for o in doc.Objects:
    if not hasattr(o, "Shape") or o.Shape.isNull():
        continue
    if not o.Shape.Faces:
        continue
    if OBJS and o.Name not in OBJS:
        continue
    grp = None
    for g in doc.Objects:
        if g.isDerivedFrom("App::Part") and o in g.Group:
            grp = g.Name
            break
    col = PART_COLOUR.get(o.Name) or GROUP_COLOUR.get(grp) or PALETTE[8]
    items.append((o.Name, o.Shape, np.array(col)))

print("objects rendered:", len(items))
for n, _, c in items[:60]:
    print("   ", n)


def tris_of(shape, tol=0.35):
    """Tessellate a shape into (n,3,3) float32 triangle array."""
    verts, facets = shape.tessellate(tol)
    V = np.array([[p.x, p.y, p.z] for p in verts], dtype=np.float32)
    F = np.array(facets, dtype=np.int32)
    if len(F) == 0:
        return np.zeros((0, 3, 3), dtype=np.float32)
    return V[F]


# Collect all triangles once, with a colour index per triangle.
all_tris = []
all_cols = []
for n, shape, col in items:
    T = tris_of(shape)
    if len(T) == 0:
        continue
    all_tris.append(T)
    all_cols.append(np.tile(col, (len(T), 1)))
if not all_tris:
    raise SystemExit("no triangles")
T = np.concatenate(all_tris, axis=0)
C = np.concatenate(all_cols, axis=0).astype(np.float32)
print("triangles:", len(T))

lo = T.reshape(-1, 3).min(axis=0)
hi = T.reshape(-1, 3).max(axis=0)
ctr = (lo + hi) * 0.5
radius = float(np.linalg.norm(hi - lo)) * 0.5

# Face normals (area-weighted by cross product magnitude) and centroids.
e1 = T[:, 1] - T[:, 0]
e2 = T[:, 2] - T[:, 0]
N = np.cross(e1, e2)
areas = np.linalg.norm(N, axis=1)
good = areas > 1e-9
N[good] /= areas[good][:, None]
cen = T.mean(axis=1)

# Light rig in world space (normalised later per view is not needed for ortho).
LIGHTS = [
    (np.array([0.45, -0.75, 0.50]), 0.62),
    (np.array([-0.60, -0.35, 0.30]), 0.25),
    (np.array([0.0, 0.0, -1.0]), 0.13),   # ambient-ish fill from below
]
for i, (d, w) in enumerate(LIGHTS):
    LIGHTS[i] = (d / np.linalg.norm(d), w)


def basis(az, el):
    a, e = np.radians(az), np.radians(el)
    # View direction: camera sits at az/el looking at the model.
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
    # Eye-space coordinates: x along `right`, y along `up`, z along `fwd` (depth).
    P = T.reshape(-1, 3) - ctr
    X = P @ right
    Y = P @ up
    Z = P @ fwd
    X = X.reshape(-1, 3)
    Y = Y.reshape(-1, 3)
    Z = Z.reshape(-1, 3)

    span = 2.0 * radius
    x0, x1 = X.min(), X.max()
    y0, y1 = Y.min(), Y.max()
    scale = (w - 12) / max(x1 - x0, 1e-6)
    h = int((y1 - y0) * scale) + 12
    h = max(h, 8)

    img = np.zeros((h, w, 3), dtype=np.float32)
    # Camera looks along +fwd, so SMALLER eye-space z is NEARER. Initialise the depth
    # buffer to +inf and let nearer fragments win. (An earlier version compared with `>`
    # against -1e30 and therefore rendered the far side of every solid.)
    zbuf = np.full((h, w), 1e30, dtype=np.float32)

    # Shading: a face is visible when its outward normal points back toward the camera.
    # With the camera looking along +fwd, visible outward normals have N.fwd < 0.
    Nv_z = N @ fwd
    # Light directions rotated into view space for shading.
    shade = np.zeros(len(T), dtype=np.float32)
    for d, wt in LIGHTS:
        shade += wt * np.clip(N @ d, 0.0, 1.0)
    shade = np.clip(0.20 + 0.80 * shade, 0.0, 1.35)
    # Mild depth cue: farther = slightly darker. Depth runs along +fwd, so larger = farther.
    zc = cen @ fwd
    zmin, zmax = zc.min(), zc.max()
    depth_fade = 1.0 - 0.22 * (1.0 - (zmax - zc) / max(zmax - zmin, 1e-6))

    px = (X - x0) * scale + 6.0
    py = (y1 - Y) * scale + 6.0

    front = Nv_z < 0.0
    order = np.argsort(zc)[::-1]           # far to near, painter's order for tie-breaks
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
        zplane = Z[i, 0] + u * (Z[i, 1] - Z[i, 0]) + vv * (Z[i, 2] - Z[i, 0])
        sub_z = zbuf[miny:maxy + 1, minx:maxx + 1]
        better = m & (zplane < sub_z)     # smaller eye-space z is nearer
        if not better.any():
            continue
        sub_z[better] = zplane[better]
        col = C[i] * shade[i] * depth_fade[i]
        sub = img[miny:maxy + 1, minx:maxx + 1]
        sub[better] = np.clip(col, 0.0, 1.0)
    return img


views = []
for az, el in zip(AZ, EL):
    views.append(render(az, el))
    print("rendered az %.0f el %.0f -> %dx%d" % (az, el, views[-1].shape[1], views[-1].shape[0]))

GAP = 8
total_h = sum(v.shape[0] + GAP for v in views)
canvas = np.full((total_h, W, 3), 1.0, dtype=np.float32)
y = 0
for v in views:
    canvas[y:y + v.shape[0], :v.shape[1]] = v
    y += v.shape[0] + GAP
    canvas[y - 4:y - 2, :, :] = 0.0

a = (canvas * 255).astype(np.uint8)
h, w, _ = a.shape
with open(OUT, "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (w, h))
    f.write(a.tobytes())
print("wrote", OUT, "%dx%d" % (w, h))
