"""Stop face placed from the MEASURED leg geometry at 65 deg.

From 55_probe_leg_outline.py the leg plate's inner face at 65 deg runs
  A = (Y 8.73, Z -2.80)  ->  B = (Y 18.36, Z -38.03)
and the whole face translates toward (-Y,-Z) as the leg opens further, so a stop placed just
beyond that face in the outward normal direction is struck exactly at 65 deg.

The stop is built in the 65 deg frame and rotated back to the folded/print frame, then tested:
  first-contact angle (want ~65), contact area at 65, and clearance when folded (want 0 clash).
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

A = (8.73, -2.80)
B = (18.36, -38.03)
ux, uz = B[0] - A[0], B[1] - A[1]
L = math.hypot(ux, uz)
ux, uz = ux / L, uz / L
nx, nz = uz, -ux                       # normal; sign chosen below
if nx > 0:
    nx, nz = -nx, -nz

COS, SIN = math.cos(math.radians(65.0)), math.sin(math.radians(65.0))


def rot_back(y, z):
    dy, dz = y - m.HINGE_Y, z - m.KNUCKLE_Z
    return (m.HINGE_Y + dy * COS - dz * SIN, m.KNUCKLE_Z + dy * SIN + dz * COS)


def build_stop(s0, s1, d, x0, x1, clear):
    """Quad in the 65 deg frame: along the face s0..s1, outward from clear to clear+d."""
    quad = []
    for s, w in ((s0, clear), (s1, clear), (s1, clear + d), (s0, clear + d)):
        quad.append((A[0] + s * ux + w * nx, A[1] + s * uz + w * nz))
    poly = [rot_back(y, z) for y, z in quad]
    wire = Part.makePolygon([App.Vector(x0, p[0], p[1]) for p in poly] +
                            [App.Vector(x0, poly[0][0], poly[0][1])])
    face = Part.Face(wire)
    return face.extrude(App.Vector(x1 - x0, 0, 0))


def evaluate(name, s0, s1, d, spans, clear):
    parts = []
    for x0, x1 in spans:
        try:
            parts.append(build_stop(s0, s1, d, x0, x1, clear))
        except Exception as e:
            print("%-26s | build failed: %s" % (name, e))
            return
    stop = parts[0]
    for p in parts[1:]:
        stop = stop.fuse(p)
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
    folded = foot.common(stop).Volume
    print("%-26s | first %5s | vol@65 %8.4f | folded %7.4f"
          % (name, ("%.1f" % fc) if fc else "never", inter.Volume, folded))


# The two wide clear spans between clip stations (clips at X 42.7-45.7, 65.7-68.7, 88.7-91.7).
SPANS = [(47.0, 64.0), (70.0, 87.0)]

print("face A %s -> B %s  len %.2f  normal (%.3f, %.3f)" % (A, B, L, nx, nz))
print("stop spans (between clips): %s" % (SPANS,))
print()
for s0, s1 in ((0.0, 6.0), (0.0, 10.0), (0.0, 14.0), (2.0, 12.0)):
    for d in (2.0, 3.0):
        evaluate("s %.0f..%.0f d %.1f c0" % (s0, s1, d), s0, s1, d, SPANS, 0.0)
for s0, s1 in ((0.0, 10.0), (0.0, 14.0)):
    evaluate("s %.0f..%.0f d 3 c-0.05" % (s0, s1), s0, s1, 3.0, SPANS, -0.05)
    evaluate("s %.0f..%.0f d 3 c+0.10" % (s0, s1), s0, s1, 3.0, SPANS, 0.10)
