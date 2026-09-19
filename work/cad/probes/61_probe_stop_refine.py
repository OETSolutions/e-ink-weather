"""Refine the stop: axis-aligned boxes in the clear gaps between clip stations, targeting
first contact at 64-66 deg with the largest bearing area at 65 deg and no clash when folded.

The gaps are X 47..64 and 70..87 (clips sit at 42.7-45.7, 65.7-68.7, 88.7-91.7).
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
for d10 in range(560, 730, 5):
    d = d10 / 10.0
    leg = foot.copy()
    leg.rotate(HINGE, AXIS, -d)
    LEGS[d] = leg

GAPS = [(47.0, 64.0), (70.0, 87.0)]


def make(y0, z0, ly, lz):
    s = None
    for x0, x1 in GAPS:
        b = Part.makeBox(x1 - x0, ly, lz, App.Vector(x0, y0, z0))
        s = b if s is None else s.fuse(b)
    return s


rows = []
for yi in range(70, 115, 5):          # Y0 7.0 .. 11.0
    y0 = yi / 10.0
    for zi in range(-115, -55, 5):    # Z0 -11.5 .. -5.5
        z0 = zi / 10.0
        for ly, lz in ((2.0, 2.0), (2.0, 3.0), (3.0, 2.0)):
            box = make(y0, z0, ly, lz)
            fc = None
            for d in sorted(LEGS):
                if LEGS[d].common(box).Volume > 0.002:
                    fc = d
                    break
            if fc is None or not (63.0 <= fc <= 67.0):
                continue
            leg65 = foot.copy()
            leg65.rotate(HINGE, AXIS, -65.0)
            inter = leg65.common(box)
            clash = foot.common(box).Volume
            if clash > 0.01:
                continue
            rows.append((inter.Volume, fc, y0, z0, ly, lz))

rows.sort(reverse=True)
print("valid stops (first contact 63..67, no folded clash), best bearing first:")
print("  vol@65 | first |  Y0    Z0  | size")
for v, fc, y0, z0, ly, lz in rows[:20]:
    print("  %6.3f | %5.1f | %5.2f %6.2f | %.1f x %.1f" % (v, fc, y0, z0, ly, lz))
if not rows:
    print("  (none)")
