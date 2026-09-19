"""Measure the wall stop: weld, root bearing, first-contact angle, clip clearance, insertion."""
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
print("cover no-stop %.1f  with-stop %.1f  adds %.1f mm3"
      % (cover_off.Volume, cover_on.Volume, cover_on.Volume - cover_off.Volume))

wall = Part.makeBox(m.STOP_BEAM_X1-m.STOP_BEAM_X0, m.STOP_WALL_Y1-m.STOP_WALL_Y0,
                    m.STOP_WALL_Z1-m.STOP_WALL_Z0,
                    App.Vector(m.STOP_BEAM_X0, m.STOP_WALL_Y0, m.STOP_WALL_Z0))
for cx in CXS:
    w = m.ARM_W/2.0 + m.STOP_CLIP_CLEAR
    wall = wall.cut(Part.makeBox(2*w, 400, 400, App.Vector(cx-w, -200, m.STOP_WALL_SHELF)))

print("\nSTOP_WALL_Y1 = %.3f" % m.STOP_WALL_Y1)
print("\n1. WELD -- stop geometry x cover-without-stop")
joint = wall.common(cover_off)
print("   stop geometry %.2f mm3 ; overlaps pre-existing cover by %.2f mm3"
      % (wall.Volume, joint.Volume))
for s in sorted(joint.Solids, key=lambda x: -x.Volume):
    bb = s.BoundBox
    print("      %7.2f mm3  X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\n2. ROOT BEARING at z -2.90..-2.85 (inside the webs)")
sec = wall.common(Part.makeBox(400, 400, 0.05, App.Vector(-200, -200, -2.90)))
print("   stop cross-section there: %.2f mm2" % (sec.Volume/0.05))

print("\n3. FIRST CONTACT vs angle")
for deg in (50, 55, 58, 60, 61, 62, 63, 64, 64.5, 65, 65.25, 65.5, 66, 67, 68, 70, 80, 90):
    f = foot.copy()
    f.rotate(HINGE, AXIS, -float(deg))
    d = f.common(cover_on).Volume - f.common(cover_off).Volume
    print("   %6.2f deg : stop-only contact %10.4f mm3 %s"
          % (deg, max(d, 0.0), "<-- FIRST" if 0.5 < d < 25 else ""))

print("\n4. CLIP SWEEP clearance")
for deg in (0, 20, 40, 50, 55, 60, 62, 64, 65, 66, 70):
    f = foot.copy()
    f.rotate(HINGE, AXIS, -float(deg))
    cb = 0.0
    for c in CXS:
        sl = Part.makeBox(3.4, 300, 300, App.Vector(c-1.7, -60, -100))
        cb += f.common(cover_on).common(sl).Volume - f.common(cover_off).common(sl).Volume
    print("   %3d deg : %8.4f mm3 %s" % (deg, max(cb, 0.0), "FOUL" if cb > 0.05 else "clear"))

print("\n5. INSERTION PATH (foot lowered from +Z, mouth faces +Z)")
for dz in range(9, -11, -1):
    f = foot.copy()
    f.translate(App.Vector(0, 0, dz))
    d = f.common(cover_on).Volume - f.common(cover_off).Volume
    print("   dz %+3d : stop contact %9.4f mm3 %s"
          % (dz, max(d, 0.0), "BLOCKED" if d > 0.05 else "clear"))

print("\n6. SEATED JOINT (folded)")
print("   foot x cover total %.3f mm3" % foot.common(cover_on).Volume)
