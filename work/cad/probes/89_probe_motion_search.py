"""Does a valid assembly motion exist? Search over foot angle phi and insertion distance s.

The clip mouth faces +Z in the foot frame, so for a foot at angle phi the insertion direction
in world is (0, sin(phi), cos(phi)). For each phi, sweep s from far out to seated and measure
the collision with the STOP alone (one boolean per step, so the grid can be fine).
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
cover_off = m.build_cover(outer, foot)
m.STOP_ENABLE = True
cover_on = m.build_cover(outer, foot)

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)

# The stop alone: everything the stop added, as a standalone shape.
stop = cover_on.cut(cover_off)
print("stop shape: %.1f mm3, %d solids" % (stop.Volume, len(stop.Solids)))

print("\ncollision with the STOP, in mm3.  '.' = clear (<0.05), digit = blocked")
print("phi \\ s " + "".join("%4d" % s for s in range(0, 21, 2)))
best = []
for phi in range(0, 95, 5):
    a = math.radians(phi)
    dy, dz = math.sin(a), math.cos(a)
    row = ""
    worst = 0.0
    for s in range(0, 21, 2):
        f = foot.copy()
        f.rotate(HINGE, AXIS, -float(phi))
        f.translate(App.Vector(0, -s * dy, -s * dz))
        v = f.common(stop).Volume
        worst = max(worst, v)
        row += "   ." if v < 0.05 else "%4.0f" % v
    best.append((worst, phi))
    print(" %3d    %s   worst %8.2f" % (phi, row, worst))

print("\nangles with the smallest worst-case collision with the stop:")
for w, phi in sorted(best)[:6]:
    print("   phi %3d : worst %.2f mm3" % (phi, w))
