"""Polar map of the leg's material around the pin axis at 65 deg, using point-in-solid
tests instead of booleans (the ring cut fails on this shape).

Answers: at which pin angle / radius is there a broad surface a stop can bear on?
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

# Use only the clip station 0 in X so the map is a clean 2-D profile.
x0 = m.ARM_CENTERS[0]
clip = foot.common(Part.makeBox(m.CLIP_W, 70.0, 70.0,
                                App.Vector(x0 - m.CLIP_W / 2, HY - 35, HZ - 35)))

for deg in (65.0,):
    leg = clip.copy()
    leg.rotate(HINGE, AXIS, -deg)
    sol = leg.Solids[0]
    print("leg cross-section at %.0f deg -- pin angle (rows) x radius (cols)" % deg)
    print("        " + "".join("%5d" % r for r in range(0, 26, 2)))
    for pa in range(0, 360, 10):
        a = math.radians(pa)
        row = ""
        for r in range(0, 26, 2):
            y = HY + r * math.sin(a)
            z = HZ + r * math.cos(a)
            inside = sol.isInside(App.Vector(x0, y, z), 0.01, False)
            row += "%5s" % ("#" if inside else ".")
        print("  %3d   %s" % (pa, row))
    print()
    print("(pin angle 0 = +Z above the axis; 90 = +Y folded side; 180 = -Z below; 270 = -Y)")
