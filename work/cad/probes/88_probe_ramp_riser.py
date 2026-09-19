"""Measure the final stop: weld (the objection), root bearing, first contact, clip sweep,
insertion path, and seated joint."""
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
CXS = m.ARM_CENTERS
print("cover no-stop %.1f  with-stop %.1f  stop adds %.1f mm3"
      % (cover_off.Volume, cover_on.Volume, cover_on.Volume - cover_off.Volume))

ca, sa = math.cos(math.radians(m.SWING_DEG)), math.sin(math.radians(m.SWING_DEG))
py, pz = m.STOP_RAMP_P
ey, ez = py + m.STOP_RAMP_LEN*ca, pz - m.STOP_RAMP_LEN*sa
by, bz = ey - m.STOP_RAMP_T*sa, ez - m.STOP_RAMP_T*ca
ay, az = py - m.STOP_RAMP_T*sa, pz - m.STOP_RAMP_T*ca
quad = [App.Vector(0, py, pz), App.Vector(0, ey, ez),
        App.Vector(0, by, bz), App.Vector(0, ay, az)]
quad.append(quad[0])
stop = None
for x0, x1 in m.STOP_SPAN_X:
    r = (Part.Face(Part.makePolygon(quad))
         .extrude(App.Vector(x1-x0, 0, 0)).translate(App.Vector(x0, 0, 0)))
    stop = r if stop is None else stop.fuse(r)
for x0, x1 in m.STOP_RISER_X:
    stop = stop.fuse(Part.makeBox(x1-x0, m.STOP_RISER_Y1-m.STOP_RISER_Y0,
                                  m.STOP_RISER_Z1-m.STOP_RISER_Z0,
                                  App.Vector(x0, m.STOP_RISER_Y0, m.STOP_RISER_Z0)))

print("\n1. WELD -- the objection. stop geometry x cover-without-stop")
joint = stop.common(cover_off)
print("   stop geometry %.2f mm3 ; overlaps pre-existing cover by %.2f mm3" % (stop.Volume, joint.Volume))
for s in sorted(joint.Solids, key=lambda x: -x.Volume):
    bb = s.BoundBox
    print("      %7.2f mm3  X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\n2. ROOT BEARING area in the z -2.90..-2.85 slab (inside the webs)")
sec = stop.common(Part.makeBox(400, 400, 0.05, App.Vector(-200, -200, -2.90)))
print("   %.2f mm2" % (sec.Volume/0.05))

print("\n3. FIRST CONTACT vs angle")
first = None
for d10 in range(500, 1001, 5):
    d = d10/10.0
    f = foot.copy()
    f.rotate(HINGE, AXIS, -float(d))
    c = max(f.common(cover_on).Volume - f.common(cover_off).Volume, 0.0)
    if first is None and c > 0.05:
        first = d
    if d10 % 10 == 0 or d > 63:
        print("   %6.1f deg : %10.4f mm3" % (d, c))
print("   FIRST CONTACT: %s deg" % first)

print("\n4. CLIP SWEEP clearance")
worst = 0.0
for deg in (0, 20, 40, 50, 55, 60, 62, 64, 65, 66, 70):
    f = foot.copy()
    f.rotate(HINGE, AXIS, -float(deg))
    cb = 0.0
    for c in CXS:
        sl = Part.makeBox(3.4, 300, 300, App.Vector(c-1.7, -60, -100))
        cb += f.common(cover_on).common(sl).Volume - f.common(cover_off).common(sl).Volume
    worst = max(worst, cb)
    print("   %3d deg : %8.4f mm3 %s" % (deg, max(cb, 0.0), "FOUL" if cb > 0.05 else "clear"))
print("   worst %.4f mm3" % worst)

print("\n5. INSERTION PATH (foot lowered from +Z; mouth now faces +Z)")
for dz in range(9, -11, -1):
    f = foot.copy()
    f.translate(App.Vector(0, 0, dz))
    d = max(f.common(cover_on).Volume - f.common(cover_off).Volume, 0.0)
    print("   dz %+3d : %9.4f mm3 %s" % (dz, d, "BLOCKED" if d > 0.05 else "clear"))

print("\n6. SEATED JOINT (folded): foot x cover %.3f mm3" % foot.common(cover_on).Volume)
