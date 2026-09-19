"""Test candidate stop boxes at the valid zone (pin angle ~150 deg, r 13..17).

The old lug bore on a corner at pin angle ~230 / r 6.5 and arrested the leg at 55 deg, below
the gravity barrier. This tests stops that bear on the leg plate's broad inner FACE instead.
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

LEGS = {}
for d10 in range(400, 800, 5):
    d = d10 / 10.0
    leg = foot.copy()
    leg.rotate(HINGE, AXIS, -d)
    LEGS[d] = leg


def test(name, y0, y1, z0, z1, x0=None, x1=None):
    x0 = m.FOOT_X if x0 is None else x0
    x1 = m.FOOT_X + m.FOOT_W if x1 is None else x1
    box = Part.makeBox(x1 - x0, y1 - y0, z1 - z0, App.Vector(x0, y0, z0))
    fc = None
    for d in sorted(LEGS):
        if LEGS[d].common(box).Volume > 0.002:
            fc = d
            break
    v65 = LEGS[65.0].common(box).Volume if 65.0 in LEGS else 0.0
    inter = LEGS[65.0].common(box)
    bb = inter.BoundBox
    print("%-26s | first %5s | vol@65 %7.4f | %s"
          % (name, ("%.1f" % fc) if fc else "never", v65,
             ("X %.1f..%.1f Y %.2f..%.2f Z %.2f..%.2f" % (bb.XMin, bb.XMax, bb.YMin,
                                                          bb.YMax, bb.ZMin, bb.ZMax))
             if v65 > 0 else "-"))


print("target: first contact 63..66 deg, large vol@65")
print()
test("full-width band", 10.0, 14.0, -16.0, -10.0)
test("full-width band tight", 10.5, 13.5, -15.4, -10.2)
test("band, clip stations only", 10.0, 14.0, -16.0, -10.0, 28.0, 34.4)
test("band deeper", 10.0, 14.0, -18.0, -11.0)
test("band shallower", 10.0, 13.0, -14.0, -9.0)
test("band r11-15", 10.5, 12.5, -13.5, -9.7)
test("band r12-16", 11.0, 13.0, -14.5, -10.5)
test("band r13-17", 11.5, 13.5, -15.4, -11.3)
test("wide Y 9..15", 9.0, 15.0, -16.0, -9.0)
