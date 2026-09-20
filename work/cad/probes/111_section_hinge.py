"""YZ cross-section through one hinge clip station, drawn to scale.

A shaded render cannot resolve a 0.5 mm clearance or show whether two parts actually
touch, and the scalar "margin" I was using before could not see a LOCAL flare. This
slices the cover, the folded foot and the open foot with the plane X = CX, rasterises
the sections to scale, and reports:
  - overlap area of cover with each foot pose (the thing that must be ZERO),
  - a radial clearance table about the hinge axis, sampled off the RASTER (isInside on
    the fused cover is unreliable), which shows the clip's outer wall and the cavity
    wall at every angle -- i.e. the local clearance, not one averaged number.

Env: SRC, OUT, CX, HY (hinge Y), KZ (knuckle Z), RSCALE (px/mm), PAD (mm).
"""
import FreeCAD as App
import numpy as np
import os

SRC = os.environ["SRC"]
OUT = os.environ["OUT"]
CX = float(os.environ.get("CX", "67.2"))
HY = float(os.environ.get("HY", "5.0"))
KZ = float(os.environ.get("KZ", "-0.68"))
RSCALE = int(os.environ.get("RSCALE", "90"))
PAD = float(os.environ.get("PAD", "6.5"))

COLOUR = {
    "PRINT_REAR_COVER": (0.70, 0.73, 0.78),
    "PRINT_FOOT": (0.95, 0.55, 0.18),
    "REFERENCE_FOOT_OPEN_65DEG": (0.30, 0.52, 0.92),
}

doc = App.openDocument(SRC)
parts = {}
for o in doc.Objects:
    if o.Name in COLOUR and hasattr(o, "Shape") and not o.Shape.isNull():
        parts[o.Name] = o.Shape


def mask_of(shape):
    wires = shape.slice(App.Vector(1, 0, 0), CX)
    yy = np.linspace(y0, y1, W, endpoint=False) + (y1 - y0) / (2 * W)
    zz = np.linspace(z0, z1, H, endpoint=False) + (z1 - z0) / (2 * H)
    GY, GZ = np.meshgrid(yy, zz)
    inside = np.zeros((H, W), dtype=bool)
    for w in wires:
        lp = []
        for e in w.OrderedEdges:
            for p in e.discretize(30):
                lp.append((p.y, p.z))
        for i in range(len(lp)):
            ay, az = lp[i]
            by, bz = lp[(i + 1) % len(lp)]
            if az == bz:
                continue
            cond = (az > GZ) != (bz > GZ)
            xint = (by - ay) * (GZ - az) / (bz - az) + ay
            inside ^= cond & (GY < xint)
    return inside


y0, y1 = HY - PAD - 4.0, HY + PAD
z0, z1 = KZ - 3.0 - PAD, KZ + 3.0 + PAD
W = int(round((y1 - y0) * RSCALE))
H = int(round((z1 - z0) * RSCALE))
img = np.full((H, W, 3), 1.0, dtype=np.float32)

masks = {}
for name in ("PRINT_REAR_COVER", "PRINT_FOOT", "REFERENCE_FOOT_OPEN_65DEG"):
    if name not in parts:
        print("MISSING part", name)
        continue
    m = mask_of(parts[name])
    masks[name] = m
    print("%-28s %7d px  = %8.3f mm2" % (name, m.sum(), m.sum() / RSCALE**2))

# paint: cover, then foot poses; overlap = red on top
if "PRINT_REAR_COVER" in masks:
    img[masks["PRINT_REAR_COVER"]] = COLOUR["PRINT_REAR_COVER"]
for nm in ("PRINT_FOOT", "REFERENCE_FOOT_OPEN_65DEG"):
    if nm in masks:
        img[masks[nm] & ~masks.get("PRINT_REAR_COVER", np.zeros_like(masks[nm]))] = COLOUR[nm]
        ov = masks[nm] & masks["PRINT_REAR_COVER"]
        img[ov] = (0.95, 0.05, 0.05)
        print("OVERLAP cover x %-28s %7d px = %8.4f mm2"
              % (nm, ov.sum(), ov.sum() / RSCALE**2))

a = (img * 255).astype(np.uint8)
with open(OUT, "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (W, H))
    f.write(a.tobytes())
print("wrote", OUT, "%dx%d  window y %.2f..%.2f  z %.2f..%.2f"
      % (W, H, y0, y1, z0, z1))

# --- radial clearance profile off the raster --------------------------------------
print("\nradial profile about (y=%.2f, z=%.2f), x=%.2f:" % (HY, KZ, CX))
print("  angle   cover_in  cover_out   foot_in  foot_out   cover->foot gap")


def radial(m, deg):
    """Return (first_r, last_r) where the mask is set along the ray, or (None, None)."""
    rs = np.arange(1.6, 6.4, 0.5 / RSCALE)
    yy = HY + rs * np.sin(np.radians(deg))
    zz = KZ + rs * np.cos(np.radians(deg))
    col = np.clip(((yy - y0) * RSCALE).astype(int), 0, W - 1)
    row = np.clip(((zz - z0) * RSCALE).astype(int), 0, H - 1)
    on = m[row, col]
    if not on.any():
        return (None, None)
    idx = np.where(on)[0]
    return (rs[idx[0]], rs[idx[-1]])


for deg in range(0, 360, 10):
    ci, co = radial(masks["PRINT_REAR_COVER"], deg)
    fi, fo = radial(masks["PRINT_FOOT"], deg)
    oi, oo = radial(masks["REFERENCE_FOOT_OPEN_65DEG"], deg)
    s = "  %4d   %s  %s   %s  %s" % (
        deg,
        "%7.2f" % ci if ci is not None else "     --",
        "%7.2f" % co if co is not None else "     --",
        "%7.2f" % fi if fi is not None else "     --",
        "%7.2f" % fo if fo is not None else "     --")
    if fo is not None and ci is not None:
        s += "   %8.2f" % (fo - ci)
    elif oo is not None and ci is not None:
        s += "   (open foot %6.2f)" % (oo - ci)
    print(s)
