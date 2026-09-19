"""Map FIRST-CONTACT angle against stop position, so the stop can be placed past the
gravity barrier (tau = 0 at ~58.5 deg, from 36_kickstand_energy.py).

A stop whose first contact is below the barrier is worse than useless: the leg jams on the
folding side, where gravity is still pulling it closed, so the stand collapses.
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


def first_contact(y0, z0, zlen):
    """Smallest open angle at which the leg overlaps a stop box at this position."""
    box = Part.makeBox(6.0, m.STOP_LUG_Y, zlen, App.Vector(m.HINGE_X0 - m.PIN_ROOT, y0, z0))
    for deg10 in range(400, 900):
        deg = deg10 / 10.0
        leg = foot.copy()
        leg.rotate(HINGE, AXIS, -deg)
        if leg.common(box).Volume > 0.002:
            return deg
    return None


print("Y0     Z0    len | first contact")
for y0 in (6.90, 7.50, 8.00, 8.50, 9.00, 9.50, 10.00, 10.50, 11.00):
    for z0, zlen in ((-5.60, 1.30), (-6.50, 1.30), (-7.50, 1.30)):
        fc = first_contact(y0, z0, zlen)
        print("%5.2f %6.2f %5.2f | %s" % (y0, z0, zlen, ("%.1f" % fc) if fc else "never"))
