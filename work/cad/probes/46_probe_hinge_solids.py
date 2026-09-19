"""Enumerate every cover solid near the hinge and flag the ones that are 'blocky'.

The user sees 'a blocky piece sticking out from the hinge pin' in the motion model. This
lists cover solids whose bounding box sits in the hinge region, plus their distance from
the pin axis, so the protrusion can be named instead of guessed at.
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

doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = doc.getObject("CASE_SHELL").Shape
foot = m.build_foot()
cover = m.build_cover(outer, foot)

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
print("hinge axis  Y %.2f  Z %.2f   pin r %.2f" % (HY, HZ, m.PIN_R))
print("cover: %d solid(s), total %.1f mm3" % (len(cover.Solids), cover.Volume))
print()
print("solids with any material in Y < 16 (the hinge band):")
for i, s in enumerate(cover.Solids):
    bb = s.BoundBox
    if bb.YMin > 16.0:
        continue
    # distance from the pin axis (an X-parallel line through HY,HZ)
    corners = [(bb.YMin, bb.ZMin), (bb.YMin, bb.ZMax), (bb.YMax, bb.ZMin), (bb.YMax, bb.ZMax)]
    dmin = min(math.hypot(y - HY, z - HZ) for y, z in corners)
    print("  [%d] vol %9.2f  X %7.2f..%7.2f  Y %6.2f..%6.2f  Z %7.2f..%7.2f  d_axis>=%.2f"
          % (i, s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax, dmin))

print()
print("cross-sections through the hinge, cutting at several X (cover material only):")
for x in (27.0, 30.0, 33.0, 36.0, 40.0, 44.2, 55.0, 80.0):
    slab = Part.makeBox(0.2, 30.0, 40.0, App.Vector(x - 0.1, HY - 10.0, HZ - 12.0))
    sec = cover.common(slab)
    if sec.Volume <= 0:
        print("  x %5.1f : (no material)" % x)
        continue
    bb = sec.BoundBox
    print("  x %5.1f : vol %7.3f  Y %6.2f..%6.2f  Z %7.2f..%7.2f"
          % (x, sec.Volume, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
    # count disjoint pieces
    pieces = []
    for s in sec.Solids:
        b = s.BoundBox
        pieces.append("Y%.2f..%.2f Z%.2f..%.2f" % (b.YMin, b.YMax, b.ZMin, b.ZMax))
    for p in pieces:
        print("        piece %s" % p)
