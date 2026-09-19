"""Stop face placed directly from the MEASURED leg geometry at 65 deg.

From 55_probe_leg_outline.py, at 65 deg the leg plate's inner face runs from
  A = (Y 8.73, Z -2.80)   (the plate's near corner, r 4.29, pa 119.6)
  B = (Y 18.36, Z -38.03) (the far end of the inner face, r 39.67, pa 160.3)
so the face direction is (B-A) normalised. The stop is a prism whose outer face is that line
offset outward by a small clearance, extruded in X, then rotated back to the folded frame.

This avoids re-deriving any transform: the numbers come from the real rotated solid.
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
foot = m.build_foot()

# Measured leg-plate inner face at 65 deg (Y, Z), from 55_probe_leg_outline.py.
A = (8.73, -2.80)
B = (18.36, -38.03)
ux, uz = B[0] - A[0], B[1] - A[1]
L = math.hypot(ux, uz)
ux, uz = ux / L, uz / L
# Outward normal (pointing away from the leg, i.e. -Y side of the face).
nx, nz = uz, -ux
if nx > 0:                      # want the normal pointing toward -Y
    nx, nz = -nx, -nz
print("face A %s -> B %s   len %.2f   normal (%.3f, %.3f)" % (A, B, L, nx, nz))

COS, SIN = math.cos(math.radians(65.0)), math.sin(math.radians(65.0))


def rot_back(y, z):
    dy, dz = y - m.HINGE_Y, z - m.KNUCKLE_Z
    return (m.HINGE_Y + dy * COS - dz * SIN, m.KNUCKLE_Z + dy * SIN + dz * COS)


def build_stop(s0, s1, d, x0, x1, clear):
    """Quad: along the face from s0..s1, from the face outward by `clear`, depth `d` behind."""
    poly65 = []
    for s in (s0, s1):
        for w in (clear, clear + d):
            y = A[0] + s * ux + w * nx
            z = A[1] + s * uz + w * nz
            poly65.append((y, z))
    poly = [rot_back(y, z) for y, z in poly65]
    wire = Part.makePolygon([App.Vector(x0, p[0], p[1]) for p in poly] +
                            [App.Vector(x0, poly[0][0], poly[0][1])])
    try:
        face = Part.Face(wire)
    except Exception as e:
        print("   face failed: %s" % e)
        return None
    return face.extrude(App.Vector(x1 - x0, 0, 0))


def evaluate(name, s0, s1, d, x0, x1, clear=0.0):
    stop = build_stop(s0, s1, d, x0, x1, clear)
    if stop is None:
        return
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
    print("%-24s | first %5s | vol@65 %8.4f | folded clash %7.4f"
          % (name, ("%.1f" % fc) if fc else "never", inter.Volume, folded))


print()
print("s = mm along the leg's inner face (0 at the plate's near corner)")
for s0, s1 in ((0.0, 6.0), (0.0, 10.0), (2.0, 10.0), (0.0, 14.0), (4.0, 14.0), (8.0, 20.0)):
    for d in (1.5, 2.5):
        evaluate("s %.0f..%.0f d %.1f" % (s0, s1, d), s0, s1, d, m.FOOT_X, m.FOOT_X + m.FOOT_W)
