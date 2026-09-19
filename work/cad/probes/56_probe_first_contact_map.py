"""First-contact map: for every point (Y,Z) near the hinge, at what open angle does the leg
first occupy it?

The stop must sit in a pocket that stays empty until ~63 deg and is filled at 65 deg. This
prints that pocket's shape so the stop can be designed to fit it, instead of guessing boxes.
Sampled at a mid-plate X where the leg is a plain prism.
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
XS = m.FOOT_X + 28.0        # mid-plate, clear of every clip station

# Leg cross-section polygon at each angle.
POLY = {}
for deg in range(30, 81):
    leg = foot.copy()
    leg.rotate(HINGE, AXIS, -float(deg))
    sec = leg.common(Part.makeBox(0.4, 100.0, 120.0,
                                  App.Vector(XS - 0.2, HY - 50, HZ - 60)))
    pts = sorted({(round(v.Point.y, 3), round(v.Point.z, 3))
                  for s in sec.Solids for v in s.Vertexes})
    POLY[deg] = pts


def hull(pts):
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


HULL = {d: hull(p) for d, p in POLY.items()}


def inside(poly, y, z):
    n = len(poly)
    if n < 3:
        return False
    sign = 0
    for i in range(n):
        y1, z1 = poly[i]
        y2, z2 = poly[(i + 1) % n]
        c = (y2 - y1) * (z - z1) - (z2 - z1) * (y - y1)
        if abs(c) < 1e-9:
            continue
        s = 1 if c > 0 else -1
        if sign == 0:
            sign = s
        elif s != sign:
            return False
    return True


def first_contact(y, z):
    for d in range(30, 81):
        if inside(HULL[d], y, z):
            return d
    return None


# Map: rows = Z (top to bottom), cols = Y.
Y0, Y1, DY = 4.0, 30.0, 1.0
Z0, Z1, DZ = -24.0, 4.0, 1.0
print("first-contact angle map (mid-plate x=%.1f)." % XS)
print("legend: '.' never(>80)  'a'<=55  'b'56-59  'c'60-62  'D'63-66  'e'67-70  'f'>70")
print()
header = "       " + "".join("%4.0f" % y for y in
                             [Y0 + i * DY for i in range(int((Y1 - Y0) / DY) + 1)])
print(header)
zs = [Z0 + i * DZ for i in range(int((Z1 - Z0) / DZ) + 1)]
for z in sorted(zs, reverse=True):
    row = ""
    for y in [Y0 + i * DY for i in range(int((Y1 - Y0) / DY) + 1)]:
        fc = first_contact(y, z)
        if fc is None:
            row += "%4s" % "."
        elif fc <= 55:
            row += "%4s" % "a"
        elif fc <= 59:
            row += "%4s" % "b"
        elif fc <= 62:
            row += "%4s" % "c"
        elif fc <= 66:
            row += "%4s" % "D"
        elif fc <= 70:
            row += "%4s" % "e"
        else:
            row += "%4s" % "f"
    print("Z %6.1f %s" % (z, row))
