"""Measure the 65-degree stop's attachment to the cover.

The user: "the stops that are attached to the same rod also just fall off because their
attachment is too weak/small".

The previous round reported 13.52 mm3 of weld with risers buried in the webs. The user says
it still falls off, so measure it AGAIN on the CURRENT solids and find where the load path
is actually thin.
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

# --- rebuild the stop exactly as the generator does ---------------------------------
ca, sa = math.cos(math.radians(m.SWING_DEG)), math.sin(math.radians(m.SWING_DEG))
py, pz = m.STOP_RAMP_P
ey, ez = py + m.STOP_RAMP_LEN * ca, pz - m.STOP_RAMP_LEN * sa
by, bz = ey - m.STOP_RAMP_T * sa, ez - m.STOP_RAMP_T * ca
ay, az = py - m.STOP_RAMP_T * sa, pz - m.STOP_RAMP_T * ca
quad = [App.Vector(0, py, pz), App.Vector(0, ey, ez),
        App.Vector(0, by, bz), App.Vector(0, ay, az)]
quad.append(quad[0])
stop = None
for x0, x1 in m.STOP_SPAN_X:
    ramp = (Part.Face(Part.makePolygon(quad))
            .extrude(App.Vector(x1 - x0, 0, 0)).translate(App.Vector(x0, 0, 0)))
    stop = ramp if stop is None else stop.fuse(ramp)
for x0, x1 in m.STOP_RISER_X:
    stop = stop.fuse(Part.makeBox(x1 - x0, m.STOP_RISER_Y1 - m.STOP_RISER_Y0,
                                  m.STOP_RISER_Z1 - m.STOP_RISER_Z0,
                                  App.Vector(x0, m.STOP_RISER_Y0, m.STOP_RISER_Z0)))

print("=== stop solid, as generated ===")
print("   volume %.3f mm3, %d solid(s)" % (stop.Volume, len(stop.Solids)))
for s in stop.Solids:
    bb = s.BoundBox
    print("      %8.3f mm3  X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

# --- what does the stop actually touch? ---------------------------------------------
src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = src.getObject("CASE_SHELL").Shape
# The cover WITHOUT the stop: rebuild by calling build_cover with STOP_ENABLE off.
m.STOP_ENABLE = False
cover_no_stop = m.build_cover(outer, m.build_foot())
m.STOP_ENABLE = True

print("\n=== stop x cover-without-stop  (the real weld) ===")
weld = stop.common(cover_no_stop)
print("   weld volume %.4f mm3 across %d solid(s)" % (weld.Volume, len(weld.Solids)))
for s in sorted(weld.Solids, key=lambda q: q.BoundBox.XMin):
    bb = s.BoundBox
    print("      %8.4f mm3  X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\n=== weld cross-section, sliced along X (the load is carried in Z here) ===")
for x in [29.5, 30.0, 30.5, 31.0, 31.5, 31.9, 32.5, 35.0, 40.0,
          93.0, 100.0, 102.5, 103.0, 103.5, 104.0, 104.5, 104.9]:
    sec = weld.common(Part.makeBox(0.02, 400, 400, App.Vector(x, -200, -200)))
    print("   x %6.1f : %.4f mm2" % (x, sec.Volume / 0.02))

print("\n=== the web: is the riser really buried in it? ===")
# Cover material in the riser's Y-Z band, ignoring the stop, at the riser's X.
for x in [30.0, 30.5, 31.0, 31.5, 103.0, 103.5, 104.0, 104.5]:
    col = cover_no_stop.common(Part.makeBox(0.02, 400, 400, App.Vector(x, -200, -200)))
    bb = col.BoundBox
    if col.Volume < 1e-6:
        print("   x %6.1f : no material" % x)
    else:
        print("   x %6.1f : area %8.3f mm2  Y %.2f..%.2f Z %.2f..%.2f"
              % (x, col.Volume / 0.02, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\n=== weld footprint: how many mm2 of the riser's top face is inside the web? ===")
# Slice the weld just under the web bottom (z -2.88) and at several z.
for z in [-5.0, -4.5, -4.0, -3.5, -3.0, -2.9, -2.88, -2.8, -2.5, -2.2, -2.0]:
    sec = weld.common(Part.makeBox(400, 400, 0.02, App.Vector(-200, -200, z)))
    print("   z %6.2f : %.4f mm2" % (z, sec.Volume / 0.02))
