"""Where is the leg, really, at each angle? And what does the stop actually touch?"""
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
foot0 = m.build_foot()
m.STOP_ENABLE = False
cover_ns = m.build_cover(outer, foot0)
m.STOP_ENABLE = True
cover_st = m.build_cover(outer, foot0)

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)

# The plate alone, so the clip geometry does not confuse the picture.
plate = m.rprism(m.FOOT_W, m.FOOT_H, 4.0, m.FOOT_X, m.FOOT_Y, 0.0, m.FOOT_T)
print("plate local bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
      % (plate.BoundBox.XMin, plate.BoundBox.XMax, plate.BoundBox.YMin,
         plate.BoundBox.YMax, plate.BoundBox.ZMin, plate.BoundBox.ZMax))

print("\n=== plate position vs angle (rotated -deg about the hinge axis) ===")
print("deg   plate bbox Y         plate bbox Z        near-edge(Y=8.5) world")
for deg in (0, 20, 40, 55, 60, 65, 70, 75, 90):
    p = plate.copy()
    p.rotate(HINGE, AXIS, -float(deg))
    bb = p.BoundBox
    e = App.Vector(m.FOOT_X + 1, m.FOOT_Y, 0.0)
    e = App.Placement(HINGE, App.Rotation(AXIS, -float(deg))).multVec(e)
    print(" %3d   %6.2f..%7.2f   %6.2f..%7.2f    Y %6.2f Z %6.2f"
          % (deg, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax, e.y, e.z))

print("\n=== foot vs cover interference, per angle ===")
print("deg  w/stop total   w/stop elsewhere   no-stop total   worst solid bbox")
for deg in (55, 58, 60, 62, 64, 65, 66, 68, 70, 75, 90, 110):
    f = foot0.copy()
    f.rotate(HINGE, AXIS, -float(deg))
    i_st = f.common(cover_st)
    i_ns = f.common(cover_ns)
    # "elsewhere" = outside the clip bands, using explicit slabs
    slabsum = 0.0
    for c in m.ARM_CENTERS:
        s = Part.makeBox(3.4, 300, 300, App.Vector(c - 1.7, -60, -100))
        slabsum += i_st.common(s).Volume
    worst = ""
    if i_ns.Solids:
        w = max(i_ns.Solids, key=lambda x: x.Volume)
        bb = w.BoundBox
        worst = ("%7.2f X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f"
                 % (w.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
    print(" %3d  %10.3f   %13.3f   %12.3f   %s"
          % (deg, i_st.Volume, i_st.Volume - slabsum, i_ns.Volume, worst))
