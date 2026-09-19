"""Sweep a 65 deg ramp's offset along its own face normal, with the ramp anchored in the webs.

The face line was measured off the real rotated solid (55_probe_leg_outline.py): through
(7.10, -3.56) with direction (cos65, -sin65). A prism whose top face lies on that line, extruded
across the full width and cut at the clip stations, gives FACE contact with the plate's
underside rather than the line contact a vertical face gives.

Offset o moves the face INTO the plate (positive) or away from it (negative) along the face
normal. We want the smallest o that first touches at exactly 65.0 deg.
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
CXS = m.ARM_CENTERS

ca = math.cos(math.radians(m.SWING_DEG))
sa = math.sin(math.radians(m.SWING_DEG))
PY, PZ = 7.10, -3.56          # measured face line point
CLIP_CLEAR = 0.60
T = 3.0                        # beam thickness along the normal
L = 14.0                       # length along the face
X0 = m.FOOT_X - 2.80           # into the left web
X1 = m.FOOT_X + m.FOOT_W + 2.80


def make_ramp(o):
    # Face line, offset along the normal (-sa, -ca) by o.
    fy, fz = PY - sa * o, PZ - ca * o
    ey, ez = fy + L * ca, fz - L * sa
    by, bz = ey - sa * T, ez - ca * T
    ay, az = fy - sa * T, fz - ca * T
    pts = [App.Vector(0, fy, fz), App.Vector(0, ey, ez),
           App.Vector(0, by, bz), App.Vector(0, ay, az)]
    pts.append(pts[0])
    ramp = (Part.Face(Part.makePolygon(pts))
            .extrude(App.Vector(X1 - X0, 0, 0)).translate(App.Vector(X0, 0, 0)))
    for cx in CXS:
        w = m.ARM_W / 2.0 + CLIP_CLEAR
        ramp = ramp.cut(Part.makeBox(2 * w, 400, 400, App.Vector(cx - w, -200, -200)))
    return ramp


print("sweep the ramp face offset; want FIRST CONTACT = 65.0 deg")
print("   o    first-contact   c@64   c@65    c@66    c@70   c@90   weld mm3")
for oi in range(-4, 13):
    o = oi * 0.10
    ramp = make_ramp(o)
    cover = cover_base.fuse(ramp)
    weld = ramp.common(cover_base).Volume

    def contact(deg):
        f = foot.copy()
        f.rotate(HINGE, AXIS, -float(deg))
        return max(f.common(cover).Volume - f.common(cover_base).Volume, 0.0)

    first = None
    for d10 in range(500, 1001, 5):
        d = d10 / 10.0
        if contact(d) > 0.05:
            first = d
            break
    print("  %5.2f   %8s   %7.3f %7.3f %7.3f %7.3f %7.3f   %7.2f"
          % (o, "--" if first is None else "%.1f" % first,
             contact(64), contact(65), contact(66), contact(70), contact(90), weld))
