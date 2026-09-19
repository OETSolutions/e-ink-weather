"""Choose the stop geometry: first contact past the gravity barrier (~58.5 deg), with a
real bearing area at 65 deg rather than a corner graze.

Candidates vary (a) how far the lug is lowered in Z and (b) whether the lug is narrowed in X
to sit fully outboard of the foot plate (plate X 32.20..102.20).
"""
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
foot = m.build_foot()
PLATE_X0, PLATE_X1 = m.FOOT_X, m.FOOT_X + m.FOOT_W
print("plate X %.2f..%.2f" % (PLATE_X0, PLATE_X1))

CANDS = [
    ("current",            6.0, 6.90, -5.60, 1.30),
    ("narrow-X 3.8",       3.8, 6.90, -5.60, 1.30),
    ("lower -6.50",        6.0, 6.90, -6.50, 1.30),
    ("lower -7.00",        6.0, 6.90, -7.00, 1.30),
    ("lower -7.50",        6.0, 6.90, -7.50, 1.30),
    ("lower -8.00",        6.0, 6.90, -8.00, 1.30),
    ("lower -7.50 y7.5",   6.0, 7.50, -7.50, 1.30),
    ("narrow+lower",       3.8, 6.90, -7.50, 1.30),
    ("wide y6.4..9.4",     6.0, 6.40, -7.50, 3.00),
]


def box_for(w, y0, z0, zlen):
    return Part.makeBox(w, m.STOP_LUG_Y, zlen,
                        App.Vector(m.HINGE_X0 - m.PIN_ROOT, y0, z0))


def first_contact(box):
    deg = 40.0
    while deg <= 90.0:
        leg = foot.copy()
        leg.rotate(HINGE, AXIS, -deg)
        if leg.common(box).Volume > 0.002:
            return deg
        deg += 0.25
    return None


print()
print("%-18s | first |  vol@65 | contact at 65 (bbox)" % "candidate")
for name, w, y0, z0, zlen in CANDS:
    box = box_for(w, y0, z0, zlen)
    fc = first_contact(box)
    leg = foot.copy()
    leg.rotate(HINGE, AXIS, -65.0)
    inter = leg.common(box)
    bb = inter.BoundBox
    where = ("X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
             % (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax)) if inter.Volume > 0 else "-"
    print("%-18s | %5s | %7.4f | %s"
          % (name, ("%.2f" % fc) if fc else "never", inter.Volume, where))
