"""Design the stop as a face parallel to the leg's plate face at 65 deg.

The old lug arrested the leg at 55 deg, which is BELOW the gravity barrier (58.5 deg from
36_kickstand_energy.py): below the barrier gravity folds the leg, so a stop there is pressed
from the wrong side and the stand collapses. The stop must first contact ABOVE the barrier.

This builds the stop in the 65 deg frame, aligned to the leg's own plate face, then rotates it
back into the folded frame (as it must be printed) and measures:
  - first-contact angle   (want 63..66)
  - contact area at 65    (want a face, not a corner)
  - clearance when folded (must be 0: no collision with the retracted leg)
"""
import os
import sys
import math

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
foot = m.build_foot()

COS, SIN = math.cos(math.radians(65.0)), math.sin(math.radians(65.0))


def rot_back(y, z):
    """65-deg frame -> folded frame (undo the leg's open rotation)."""
    dy, dz = y - HY, z - HZ
    return (HY + dy * COS - dz * SIN, HZ + dy * SIN + dz * COS)


def build_stop(s0, s1, d, x0, x1):
    """Prism whose contact face is the leg's plate face at 65 deg."""
    P0 = (HY + (m.FOOT_Y - HY) * COS + (m.FOOT_T - HZ) * SIN,
          HZ - (m.FOOT_Y - HY) * SIN + (m.FOOT_T - HZ) * COS)
    u = (SIN, COS)          # along the face, away from the hinge
    # face direction at 65 deg is (sin, cos) rotated; use the measured corner instead
    u = (COS, -SIN)
    v = (SIN, COS)
    pts = []
    for s in (s0, s1):
        for w in (0.0, d):
            y = P0[0] + s * u[0] + w * v[0]
            z = P0[1] + s * u[1] + w * v[1]
            fy, fz = rot_back(y, z)
            pts.append((fy, fz))
    # In the folded frame this quad is a flat plate; extrude in X.
    ys = [p[0] for p in pts]
    zs = [p[1] for p in pts]
    box = Part.makeBox(x1 - x0, max(ys) - min(ys), max(zs) - min(zs),
                       App.Vector(x0, min(ys), min(zs)))
    # Trim to the true quad with a prism built from the quad polygon.
    poly = Part.makePolygon([App.Vector(x0, p[0], p[1]) for p in pts] +
                            [App.Vector(x0, pts[0][0], pts[0][1])])
    face = Part.Face(poly)
    return face.extrude(App.Vector(x1 - x0, 0, 0))


def evaluate(name, s0, s1, d, x0, x1):
    stop = build_stop(s0, s1, d, x0, x1)
    fc = None
    for d10 in range(400, 800, 5):
        deg = d10 / 10.0
        leg = foot.copy()
        leg.rotate(HINGE, AXIS, -deg)
        if leg.common(stop).Volume > 0.002:
            fc = deg
            break
    leg65 = foot.copy()
    leg65.rotate(HINGE, AXIS, -65.0)
    inter = leg65.common(stop)
    leg0 = foot.copy()
    folded = leg0.common(stop).Volume
    print("%-22s | first %5s | vol@65 %8.4f | folded clash %7.4f"
          % (name, ("%.1f" % fc) if fc else "never", inter.Volume, folded))
    return fc, inter.Volume, folded


print("stop built in the 65 deg frame from the leg's own plate face, rotated back to print")
print("(s = mm along the face from the plate's near corner; d = depth behind the face)")
print()
for s0, s1 in ((0.0, 10.0), (0.0, 20.0), (0.0, 30.0), (5.0, 20.0), (5.0, 30.0), (10.0, 30.0)):
    for d in (2.0, 3.0):
        evaluate("s %.0f..%.0f d %.0f" % (s0, s1, d), s0, s1, d, m.FOOT_X, m.FOOT_X + m.FOOT_W)
