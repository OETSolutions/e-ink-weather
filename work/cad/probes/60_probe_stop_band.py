"""Place the stop along the empirically-mapped 'D' band from 56_probe_first_contact_map.py.

The map (cover-frame Y,Z, mid-plate X) shows a diagonal band whose first-contact angle is
63-66 deg -- exactly what is wanted -- running from about (Y 8, Z -6) to (Y 14, Z -24).
That band is the leg plate's leading edge arriving at the stop. This builds a wall ALONG the
band, sweeps its position and thickness, and reports first contact, area at 65 deg and any
clash with the folded leg.
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

SPANS = [(47.0, 64.0), (70.0, 87.0)]


def band_wall(p0, p1, thick, n, x0, x1):
    """Quad along p0->p1 in cover-frame (Y,Z), extruded `thick` in direction n, then in X."""
    quad = [p0, p1,
            (p1[0] + thick * n[0], p1[1] + thick * n[1]),
            (p0[0] + thick * n[0], p0[1] + thick * n[1])]
    wire = Part.makePolygon([App.Vector(x0, q[0], q[1]) for q in quad] +
                            [App.Vector(x0, quad[0][0], quad[0][1])])
    return Part.Face(wire).extrude(App.Vector(x1 - x0, 0, 0))


def evaluate(name, p0, p1, thick, n):
    stop = None
    for x0, x1 in SPANS:
        w = band_wall(p0, p1, thick, n, x0, x1)
        stop = w if stop is None else stop.fuse(w)
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
    bb = inter.BoundBox
    print("%-30s | first %5s | vol@65 %7.4f | clash %7.4f | %s"
          % (name, ("%.1f" % fc) if fc else "never", inter.Volume, clash,
             ("Y %.1f..%.1f Z %.1f..%.1f" % (bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
             if inter.Volume > 0 else "-"))


# Band direction from the map: (dY,dZ) = (6,-18) normalised.
U = (6.0 / math.hypot(6, 18), -18.0 / math.hypot(6, 18))
N = (U[1], -U[0])                    # perpendicular
print("band direction (%.3f, %.3f);  perpendicular (%.3f, %.3f)" % (U + N))
print()
for y0, z0 in ((8.0, -6.0), (7.0, -5.0), (9.0, -7.0)):
    for ln in (12.0, 18.0):
        p0 = (y0, z0)
        p1 = (y0 + ln * U[0], z0 + ln * U[1])
        for thick in (2.0, 2.5):
            evaluate("p0 %s len %.0f t %.1f" % (p0, ln, thick), p0, p1, thick, N)
            evaluate("p0 %s len %.0f t -%.1f" % (p0, ln, thick), p0, p1, -thick, N)
