"""Sweep the ramp's outer extent. Find the largest ramp that (a) still stops at 65 deg and
(b) leaves the straight-down insertion path clear.

The ramp lies along the plate's underside line through (7.10, -3.56), direction (cos65,-sin65).
Its outer end Y is swept; at Y >= the plate's own edge (8.5) it sits under the plate's corner
and blocks the descent, so there should be a cutoff.
"""
import os
import sys
import math

import FreeCAD as App
import Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = doc.getObject("CASE_SHELL").Shape
foot = m.build_foot()
m.STOP_ENABLE = False
cover_base = m.build_cover(outer, foot)

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
ca = math.cos(math.radians(m.SWING_DEG))
sa = math.sin(math.radians(m.SWING_DEG))
PY, PZ = 7.10, -3.56
T = 3.0

print("plate edge Y = %.2f ; ramp face starts at Y %.2f" % (m.FOOT_Y, PY))
print("\n  Yend   L      first-contact  c@65     descent worst (dz -6..-11)")
for yend in [round(7.30 + i * 0.20, 2) for i in range(14)]:
    L = (yend - PY) / ca
    ey, ez = PY + L * ca, PZ - L * sa
    by, bz = ey - T * sa, ez - T * ca
    ay, az = PY - T * sa, PZ - T * ca
    quad = [App.Vector(0, PY, PZ), App.Vector(0, ey, ez),
            App.Vector(0, by, bz), App.Vector(0, ay, az)]
    quad.append(quad[0])
    stop = None
    for x0, x1 in m.STOP_SPAN_X:
        r = (Part.Face(Part.makePolygon(quad))
             .extrude(App.Vector(x1 - x0, 0, 0)).translate(App.Vector(x0, 0, 0)))
        stop = r if stop is None else stop.fuse(r)
    for x0, x1 in m.STOP_RISER_X:
        stop = stop.fuse(Part.makeBox(x1 - x0, m.STOP_RISER_Y1 - m.STOP_RISER_Y0,
                                      m.STOP_RISER_Z1 - m.STOP_RISER_Z0,
                                      App.Vector(x0, m.STOP_RISER_Y0, m.STOP_RISER_Z0)))
    cover = cover_base.fuse(stop)

    def contact(deg):
        f = foot.copy()
        f.rotate(HINGE, AXIS, -float(deg))
        return max(f.common(cover).Volume - f.common(cover_base).Volume, 0.0)

    first = None
    for d10 in range(580, 1001, 5):
        d = d10 / 10.0
        if contact(d) > 0.05:
            first = d
            break
    worst = 0.0
    for dz in range(-6, -12, -1):
        f = foot.copy()
        f.translate(App.Vector(0, 0, dz))
        worst = max(worst, max(f.common(cover).Volume - f.common(cover_base).Volume, 0.0))
    print("  %5.2f  %5.2f   %8s   %7.3f   %8.3f  %s"
          % (yend, L, "--" if first is None else "%.1f" % first, contact(65), worst,
             "OK" if worst < 0.05 else "BLOCKED"))
