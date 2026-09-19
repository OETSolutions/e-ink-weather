"""Print the leg's Y-Z outline at several open angles, so the stop can be placed against a
real face. Uses the convex hull of the rotated solid's vertices (the leg is a prism in X).
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


def hull(pts):
    pts = sorted(set(pts))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lo = []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    up = []
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0:
            up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]


# Only the plate region (exclude the clips) by sampling the leg at a mid-plate X.
XS = m.FOOT_X + 30.0
for deg in (0.0, 40.0, 55.0, 60.0, 65.0, 70.0):
    leg = foot.copy()
    leg.rotate(HINGE, AXIS, -deg)
    sec = leg.common(Part.makeBox(0.4, 80.0, 90.0,
                                  App.Vector(XS - 0.2, HY - 40, HZ - 45)))
    pts = [(round(v.Point.y, 2), round(v.Point.z, 2))
           for s in sec.Solids for v in s.Vertexes]
    h = hull(pts)
    print("open %4.0f deg  (x=%.1f, plate only):" % (deg, XS))
    for y, z in h:
        r = math.hypot(y - HY, z - HZ)
        pa = math.degrees(math.atan2(y - HY, z - HZ)) % 360.0
        print("     Y %7.2f Z %7.2f   r %5.2f  pa %5.1f" % (y, z, r, pa))
    print()
