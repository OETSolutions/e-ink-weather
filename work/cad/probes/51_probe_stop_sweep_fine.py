"""Fine sweep for a stop whose FIRST contact is just past the gravity barrier (~58.5 deg,
from 36_kickstand_energy.py) while still bearing on real area at 65 deg.

Scoring: want first-contact 63.0..66.5 deg (so the leg reaches the stop and gravity, which
opens the leg above 58.5 deg, then presses it home) and the largest contact volume at 65 deg.
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

# Precompute rotated legs once; the sweep is then cheap.
LEGS = {}
for d10 in range(560, 800):
    d = d10 / 10.0
    leg = foot.copy()
    leg.rotate(HINGE, AXIS, -d)
    LEGS[d] = leg


def evaluate(y0, z0, zlen, w):
    box = Part.makeBox(w, m.STOP_LUG_Y, zlen,
                       App.Vector(m.HINGE_X0 - m.PIN_ROOT, y0, z0))
    fc = None
    for d10 in range(560, 800):
        d = d10 / 10.0
        if LEGS[d].common(box).Volume > 0.002:
            fc = d
            break
    v65 = LEGS[65.0].common(box).Volume if 65.0 in LEGS else 0.0
    return fc, v65


rows = []
for yi in range(46, 76, 2):          # y0 4.6 .. 7.4
    y0 = yi / 10.0
    for zi in range(-68, -28, 2):    # z0 -6.8 .. -3.0
        z0 = zi / 10.0
        fc, v65 = evaluate(y0, z0, 1.30, 6.0)
        if fc is None:
            continue
        if 63.0 <= fc <= 66.5:
            rows.append((v65, fc, y0, z0))

rows.sort(reverse=True)
print("first contact in 63..66.5 deg, ranked by contact volume at 65 deg:")
print("  vol@65 | first |  Y0    Z0")
for v65, fc, y0, z0 in rows[:20]:
    print("  %6.4f | %5.2f | %5.2f %6.2f" % (v65, fc, y0, z0))
if not rows:
    print("  (none)")
