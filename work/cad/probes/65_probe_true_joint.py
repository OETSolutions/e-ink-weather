"""Measure the TRUE joints: ramp-to-cover, and clip-to-plate. Plus the real assembly path.

The previous measurement subtracted cover_off from cover_on, which also removed the part of
the ramp that lies INSIDE the cover -- so it always read zero. This builds the ramp geometry
standalone and intersects it directly.
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

print("cover WITH stop : %d solid(s)  %.1f mm3" % (len(cover_on.Solids), cover_on.Volume))
print("cover WITHOUT   : %d solid(s)  %.1f mm3" % (len(cover_off.Solids), cover_off.Volume))

# Rebuild the ramp exactly as the generator does.
ca, sa = math.cos(math.radians(m.SWING_DEG)), math.sin(math.radians(m.SWING_DEG))
py, pz = m.STOP_RAMP_P
ramp = None
for x0, x1 in ((47.0, 64.0), (70.0, 87.0)):
    ax, az = py - m.STOP_RAMP_EXT * ca, pz + m.STOP_RAMP_EXT * sa
    bx, bz = py + m.STOP_RAMP_LEN * ca, pz - m.STOP_RAMP_LEN * sa
    quad = [App.Vector(x0, ax, az), App.Vector(x0, bx, bz),
            App.Vector(x0, bx, bz - m.STOP_RAMP_DEPTH),
            App.Vector(x0, ax, az - m.STOP_RAMP_DEPTH), App.Vector(x0, ax, az)]
    r = Part.Face(Part.makePolygon(quad)).extrude(App.Vector(x1 - x0, 0, 0))
    ramp = r if ramp is None else ramp.fuse(r)

print("\n=== (a) RAMP-TO-COVER JOINT ===")
print("ramp geometry %.1f mm3" % ramp.Volume)
joint = ramp.common(cover_off)
print("ramp actually overlaps the cover by %.3f mm3" % joint.Volume)
if joint.Volume > 0:
    for s in joint.Solids:
        bb = s.BoundBox
        print("   joint solid %6.3f mm3  X %6.2f..%6.2f Y %6.2f..%6.2f Z %6.2f..%6.2f"
              % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\njoint cross-section, sliced along Z (this is the weld area that must carry the load):")
for zi in range(-300, 40, 20):
    z0 = zi / 10.0
    slab = Part.makeBox(120.0, 40.0, 0.4, App.Vector(25.0, -5.0, z0))
    v = joint.common(slab)
    if v.Volume > 0.0002:
        bb = v.BoundBox
        print("   z %6.1f : %.3f mm2   (X %.2f..%.2f Y %.2f..%.2f)"
              % (z0, v.Volume / 0.4, bb.XMin, bb.XMax, bb.YMin, bb.YMax))
