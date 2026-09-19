"""At 65 deg, what foot material sits near the hinge axis? Pick a stop that bears on a FACE.

Lists the leg's material in concentric Y-Z bands around the pin axis, so a stop can be placed
where it meets a broad flat surface rather than a corner graze.
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

leg = foot.copy()
leg.rotate(HINGE, AXIS, -65.0)

# One clip station, isolated in X so the profile is readable.
x0 = m.ARM_CENTERS[0]
slab = Part.makeBox(m.CLIP_W, 60.0, 60.0, App.Vector(x0 - m.CLIP_W / 2, HY - 30, HZ - 30))
sec = leg.common(slab)
print("clip station 0, leg cross-section at 65 deg: vol %.3f" % sec.Volume)
print("pieces:")
for s in sec.Solids:
    bb = s.BoundBox
    print("   vol %7.3f  Y %6.2f..%6.2f  Z %7.2f..%7.2f" %
          (s.Volume, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print()
print("leg material by radius from the pin axis (station 0), 65 deg:")
for r0 in range(0, 44, 2):
    r1 = r0 + 2
    ring = Part.makeCylinder(r1, m.CLIP_W, App.Vector(x0 - m.CLIP_W / 2, HY, HZ),
                             App.Vector(1, 0, 0)).cut(
           Part.makeCylinder(r0, m.CLIP_W + 0.2, App.Vector(x0 - m.CLIP_W / 2 - 0.1, HY, HZ),
                             App.Vector(1, 0, 0)))
    v = leg.common(ring).Volume
    if v > 0.001:
        # angular extent, measured in the pin frame
        angs = []
        for s in leg.common(ring).Solids:
            for vx in s.Vertexes:
                p = vx.Point
                angs.append(math.degrees(math.atan2(p.y - HY, p.z - HZ)) % 360.0)
        print("  r %2d..%2d : vol %7.3f  pin-angle %s" %
              (r0, r1, v, ("%.0f..%.0f" % (min(angs), max(angs))) if angs else "-"))
