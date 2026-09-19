"""Empirical sweep: where must a stop face be to give first contact at exactly 65 deg?

Builds a stop block from each web inward, with the front face at a swept Y, and measures the
first-contact angle. This settles the geometry by measurement instead of by my rotation algebra,
which has been wrong twice.
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
cover_base = m.build_cover(outer, foot)

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)

print("plate corner (FOOT_Y=%.1f, Z=0) world position vs angle, MEASURED by rotating the point"
      % m.FOOT_Y)
c0 = App.Vector(0, m.FOOT_Y, 0.0)
for deg in (55, 60, 63, 64, 65, 66, 67, 70, 75, 90):
    p = App.Placement(HINGE, App.Rotation(AXIS, -float(deg))).multVec(c0)
    print("   %3d deg : corner Y %.3f Z %.3f" % (deg, p.y, p.z))

print("\nplate corner (FOOT_Y=%.1f, Z=%.1f) vs angle" % (m.FOOT_Y, m.FOOT_T))
c1 = App.Vector(0, m.FOOT_Y, m.FOOT_T)
for deg in (60, 64, 65, 66, 70, 90):
    p = App.Placement(HINGE, App.Rotation(AXIS, -float(deg))).multVec(c1)
    print("   %3d deg : corner Y %.3f Z %.3f" % (deg, p.y, p.z))

print("\nSWEEP: block front face at Y = FACE, X in the two end bands, Z -4.7..-2.0")
print("  FACE   first-contact angle   contact@65   contact@66   contact@70   contact@90")
for face in [round(6.30 + i * 0.10, 2) for i in range(14)]:
    blocks = None
    for x0, x1 in m.STOP_BLK_X:
        b = Part.makeBox(x1 - x0, face - 1.0, 2.7, App.Vector(x0, 1.0, -4.70))
        blocks = b if blocks is None else blocks.fuse(b)
    cover = cover_base.fuse(blocks)

    def contact(deg):
        f = foot.copy()
        f.rotate(HINGE, AXIS, -float(deg))
        return max(f.common(cover).Volume - f.common(cover_base).Volume, 0.0)

    first = None
    for d10 in range(550, 951, 5):
        d = d10 / 10.0
        if contact(d) > 0.05:
            first = d
            break
    print("  %5.2f   %8s   %9.4f   %9.4f   %9.4f   %9.4f"
          % (face, "--" if first is None else "%.1f" % first,
             contact(65), contact(66), contact(70), contact(90)))
