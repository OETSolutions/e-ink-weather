"""Sweep the OUTER region (the leg plate's broad face lands there at 65 deg).

At 65 deg the leg plate's inner face spans pin angle 135.5..151.7 deg at radius 7.4..43.6
(computed from the real rotated solid). The stop must therefore sit around radius 7-10, not
the cramped 2-6 band the old lug used. Sweep (y0, z0) there.
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

LEGS = {}
for d10 in range(560, 800, 5):
    d = d10 / 10.0
    leg = foot.copy()
    leg.rotate(HINGE, AXIS, -d)
    LEGS[d] = leg
LEGS[65.0] = m.build_foot()
LEGS[65.0].rotate(HINGE, AXIS, -65.0)


def evaluate(y0, z0, ylen, zlen, w):
    box = Part.makeBox(w, ylen, zlen, App.Vector(m.HINGE_X0 - m.PIN_ROOT, y0, z0))
    fc = None
    for d in sorted(LEGS):
        if LEGS[d].common(box).Volume > 0.002:
            fc = d
            break
    inter = LEGS[65.0].common(box)
    bb = inter.BoundBox
    ext = sorted([bb.XLength, bb.YLength, bb.ZLength])
    return fc, inter.Volume, ext


print("y0    z0   | first | vol@65 | bbox Ylen x Zlen")
rows = []
for yi in range(80, 150, 2):
    y0 = yi / 10.0
    for zi in range(-90, -35, 2):
        z0 = zi / 10.0
        fc, v65, ext = evaluate(y0, z0, 2.0, 2.0, 6.0)
        if fc is None:
            continue
        rows.append((v65, fc, y0, z0, ext))
rows.sort(key=lambda r: (r[1], -r[0]))
for v65, fc, y0, z0, ext in rows[:25]:
    print("%5.2f %6.2f | %5.2f | %6.4f | %.2f x %.2f"
          % (y0, z0, fc, v65, ext[1], ext[2]))
if not rows:
    print("(none)")
