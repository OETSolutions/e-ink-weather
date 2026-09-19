"""Measure the two things the user is pointing at:

(a) "super thin flanges on the shaft that will just break off since the attachment is so thin"
    -- the stop ramps: how much do they actually overlap the cover, and what is the smallest
    cross-section of that joint?

(b) "there's no way to assemble the stand on the shaft" -- the clips: can the foot actually be
    brought onto the pin, and along which motion?
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

m.STOP_ENABLE = True
cover_on = m.build_cover(outer, foot)
m.STOP_ENABLE = False
cover_off = m.build_cover(outer, foot)
m.STOP_ENABLE = True

ramps = cover_on.cut(cover_off)
print("=== (a) STOP RAMPS ===")
print("ramp total volume %.1f mm3 in %d solid(s)" % (ramps.Volume, len(ramps.Solids)))
for s in ramps.Solids:
    bb = s.BoundBox
    print("   solid vol %7.2f  X %6.2f..%6.2f  Y %6.2f..%6.2f  Z %6.2f..%6.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

# Where does the ramp meet the cover (excluding the ramp itself)?
cover_only = cover_off
joint = ramps.common(cover_only)
print("\nramp-to-cover overlap: %.3f mm3" % joint.Volume)
if joint.Volume > 0:
    bb = joint.BoundBox
    print("   joint bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
          % (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

# Smallest cross-section of the joint: slice thin slabs along Z and Y.
print("\njoint cross-section, sliced along Z (thickness in X-Y):")
for zi in range(-220, 20, 20):
    z0 = zi / 10.0
    slab = Part.makeBox(120.0, 40.0, 0.4, App.Vector(25.0, -5.0, z0))
    v = ramps.common(cover_only).common(slab)
    if v.Volume > 0.0005:
        bb = v.BoundBox
        print("   z %6.1f : area-equivalent %.3f mm2  (Y %.2f..%.2f)"
              % (z0, v.Volume / 0.4, bb.YMin, bb.YMax))

print("\n=== (b) CAN THE FOOT BE PUT ON THE PIN? ===")
# The clips sit at X 44.2/67.2/90.2. Test the intended motion: bring the foot in along +Z
# (from behind the case, plate-first) and measure the clash, then try rotations.
HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)

for dz in (0.0, 20.0, 30.0, 40.0, 50.0, 60.0):
    g = foot.copy()
    if dz:
        g.translate(App.Vector(0, 0, -dz))
    clash = g.common(cover_on).Volume
    print("   pressed in from -Z by %5.1f mm : clash %8.3f mm3" % (dz, clash))

print()
for deg in (90, 75, 65, 55, 45, 30, 0):
    g = foot.copy()
    if deg:
        g.rotate(HINGE, AXIS, -float(deg))
    clash = g.common(cover_on).Volume
    print("   rotated to open %3d deg : clash %8.3f mm3" % (deg, clash))
