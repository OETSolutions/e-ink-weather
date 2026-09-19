"""Design the 65 deg stop as a RAMP parallel to the leg plate's underside at 65 deg.

Why the old stop failed: it arrested the leg at 55 deg, but 36_kickstand_energy.py shows
tau = dU/dphi changes sign at ~58.5 deg. Below that gravity FOLDS the leg; above it gravity
OPENS the leg. A stop below the barrier is pressed from the wrong side -- the leg folds away
from it and the stand collapses. That is exactly the reported failure.

The leg plate's underside at 65 deg is the line through (7.10, -3.56) with direction
(cos65, -sin65) = (0.4226, -0.9063), measured from the real rotated solid in
55_probe_leg_outline.py. A ramp whose top face IS that line contacts the plate's whole
underside at 65 deg, is clear below 65, and blocks everything above. The ramp body sits below
the line, and the folded plate at those Y only occupies Z 0..1.8, so there is no folded clash.
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

GAPS = [(47.0, 64.0), (70.0, 87.0)]      # clear spans between the three clips

COS, SIN = math.cos(math.radians(65.0)), math.sin(math.radians(65.0))
U = (COS, -SIN)                           # leg plate underside direction at 65 deg
P1 = (7.10, -3.56)                        # leg plate's near corner on the underside at 65 deg


def ramp(ext, L, D, x0, x1):
    """Top face from P1 - ext*U to P1 + L*U, body extending D in -Z."""
    A = (P1[0] - ext * U[0], P1[1] - ext * U[1])
    B = (P1[0] + L * U[0], P1[1] + L * U[1])
    quad = [A, B, (B[0], B[1] - D), (A[0], A[1] - D)]
    wire = Part.makePolygon([App.Vector(x0, q[0], q[1]) for q in quad] +
                            [App.Vector(x0, quad[0][0], quad[0][1])])
    return Part.Face(wire).extrude(App.Vector(x1 - x0, 0, 0))


def evaluate(name, ext, L, D):
    stop = None
    for x0, x1 in GAPS:
        p = ramp(ext, L, D, x0, x1)
        stop = p if stop is None else stop.fuse(p)
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
    clash = foot.common(stop).Volume
    # does the ramp actually reach the cover? test overlap with the cover's hinge material
    cover_ref = Part.makeBox(120.0, 4.4, 4.4, App.Vector(30.0, 2.8, -2.88))
    reach = stop.common(cover_ref).Volume
    bb = inter.BoundBox
    print("%-28s | first %5s | vol@65 %7.4f | folded %7.4f | reach %6.3f | %s"
          % (name, ("%.1f" % fc) if fc else "never", inter.Volume, clash, reach,
             ("Y %.1f..%.1f Z %.1f..%.1f" % (bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
             if inter.Volume > 0 else "-"))


print("ramp top face = the leg plate's underside at exactly 65 deg")
print("ext = how far the ramp runs back past P1 (to fuse with the cover)")
print()
for ext in (0.8, 1.2, 1.8):
    for L in (10.0, 14.0, 18.0):
        for D in (2.5, 3.5):
            evaluate("ext %.1f L %.0f D %.1f" % (ext, L, D), ext, L, D)
