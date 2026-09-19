"""Find where the stop can actually be ANCHORED, and where the foot can be assembled.

(a) What cover material exists near the hinge that a stop can grow from? The pin is a cylinder
    r 2.2 about (Y 5.00, Z -0.68). The end webs and the lower wall are the only structure.
(b) The clip mouth faces -Y. Work out the real assembly motion: which direction brings the
    pin through the mouth.
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

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
print("pin axis Y %.2f Z %.2f r %.2f" % (HY, HZ, m.PIN_R))
print("clip mouth faces -Y; bore r %.2f, clip wall %.2f" % (m.BORE_R, m.CLIP_WALL))

print("\n=== cover material in the hinge neighbourhood (Z -12..3, Y 0..16) ===")
for zi in range(-120, 30, 10):
    z0 = zi / 10.0
    slab = Part.makeBox(130.0, 16.0, 0.5, App.Vector(3.0, 0.0, z0))
    v = cover_off.common(slab)
    if v.Volume > 0.01:
        pieces = []
        for s in v.Solids:
            bb = s.BoundBox
            pieces.append("Y%.2f..%.2f" % (bb.YMin, bb.YMax))
        print("  z %6.1f : %8.2f mm3   %s" % (z0, v.Volume, "  ".join(pieces)))

print("\n=== cover material by X (the end webs) ===")
for xi in range(240, 340, 10):
    x0 = xi / 10.0
    slab = Part.makeBox(0.5, 20.0, 20.0, App.Vector(x0, -3.0, -14.0))
    v = cover_off.common(slab)
    if v.Volume > 0.01:
        bb = v.BoundBox
        print("  x %6.1f : %8.2f mm3   Y %.2f..%.2f Z %.2f..%.2f"
              % (x0, v.Volume, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\n=== where is the pin, in X? ===")
print("  HINGE_X0 %.2f  HINGE_X1 %.2f  PIN_ROOT %.2f" % (m.HINGE_X0, m.HINGE_X1, m.PIN_ROOT))
print("  pin spans X %.2f..%.2f" % (m.HINGE_X0 - m.PIN_ROOT, m.HINGE_X1 + m.PIN_ROOT))
print("  clip stations X %s" % m.ARM_CENTERS)
print("  plate spans X %.2f..%.2f" % (m.FOOT_X, m.FOOT_X + m.FOOT_W))
