"""Diagnose the assembly path: what blocks the foot from seating on the pin?

The clip mouth faces -Z (measured: 137 deg open arc centred on pin angle 180), so the pin
must enter by moving the foot in +Z from behind the case. Straight-line insertion clashes by
41.8 mm3 at an 8 mm offset. This finds WHERE that clash is, and whether a rotation clears it.
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
cover = m.build_cover(outer, foot)          # WITHOUT ramps: isolate the cover's own material
m.STOP_ENABLE = True

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
CXS = m.ARM_CENTERS
print("clips at X %s; mouth faces -Z; pin axis Y %.2f Z %.2f" % (CXS, HY, HZ))
print()

print("=== straight +Z insertion, clash location ===")
for dz in (-10.0, -8.0, -6.0, -5.0, -4.0, -3.0, -2.0, -1.0, 0.0):
    g = foot.copy()
    g.translate(App.Vector(0, 0, dz))
    inter = g.common(cover)
    if inter.Volume <= 0.001:
        print("  offset %5.1f : clear" % dz)
        continue
    # Is the clash inside a clip band (elastic, OK) or elsewhere (a real block)?
    inband = offband = 0.0
    for s in inter.Solids:
        cx = (s.BoundBox.XMin + s.BoundBox.XMax) / 2.0
        if any(abs(cx - c) <= 1.7 for c in CXS):
            inband += s.Volume
        else:
            offband += s.Volume
    bb = inter.BoundBox
    print("  offset %5.1f : total %8.3f | clip-band %8.3f | ELSEWHERE %8.3f | X %.1f..%.1f Y %.2f..%.2f Z %.2f..%.2f"
          % (dz, inter.Volume, inband, offband, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\n=== what is the foot hitting at offset -4 (the worst case)? ===")
g = foot.copy()
g.translate(App.Vector(0, 0, -4.0))
inter = g.common(cover)
for s in sorted(inter.Solids, key=lambda x: -x.Volume)[:6]:
    bb = s.BoundBox
    cx = (bb.XMin + bb.XMax) / 2.0
    tag = "clip band" if any(abs(cx - c) <= 1.7 for c in CXS) else "COVER BODY"
    print("   %8.3f mm3  X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f   %s"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax, tag))

print("\n=== rotational path: swing the foot in about the pin while pressing +Z ===")
HINGE = App.Vector(0, HY, HZ)
AXIS = App.Vector(1, 0, 0)
for deg in (0, 10, 20, 30, 45, 60, 75, 90):
    for dz in (-8.0, -6.0, -4.0, -2.0):
        g = foot.copy()
        g.rotate(HINGE, AXIS, -float(deg))
        g.translate(App.Vector(0, 0, dz))
        inter = g.common(cover)
        off = 0.0
        for s in inter.Solids:
            cx = (s.BoundBox.XMin + s.BoundBox.XMax) / 2.0
            if not any(abs(cx - c) <= 1.7 for c in CXS):
                off += s.Volume
        if off < 0.05:
            print("   open %2d deg, offset %5.1f : CLEAR (off-band %.4f)" % (deg, dz, off))
            break
    else:
        print("   open %2d deg : blocked at every offset tested" % deg)
