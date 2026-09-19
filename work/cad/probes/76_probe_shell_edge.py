"""What does the OUTER SHELL look like at the bottom edge? And where is the pocket skin?

This is the unknown that decides where a stop can be anchored.
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
print("outer bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
      % (outer.BoundBox.XMin, outer.BoundBox.XMax, outer.BoundBox.YMin,
         outer.BoundBox.YMax, outer.BoundBox.ZMin, outer.BoundBox.ZMax))

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
print("hinge axis Y %.2f Z %.2f ; pin r %.2f -> pin spans Y %.2f..%.2f Z %.2f..%.2f"
      % (HY, HZ, m.PIN_R, HY - m.PIN_R, HY + m.PIN_R, HZ - m.PIN_R, HZ + m.PIN_R))

print("\n=== OUTER SHELL cross-section at several X (Y across, Z down) ===")
Y0, Y1, Z0, Z1, D = -3.0, 20.0, -8.0, 4.0, 0.5
print("      " + "".join("%d" % (abs(int(y)) % 10) for y in
                         [Y0 + i * D for i in range(int((Y1 - Y0) / D))]))
for x in (36.0, 44.2, 56.0, 67.2, 80.0, 95.0):
    print("  -- X %.1f" % x)
    for j in range(int((Z1 - Z0) / D)):
        z = Z0 + j * D
        row = ""
        for i in range(int((Y1 - Y0) / D)):
            y = Y0 + i * D
            n = 0
            for dy in (0.16, 0.34):
                for dz in (0.16, 0.34):
                    if outer.isInside(App.Vector(x, y + dy, z + dz), 1e-6, True):
                        n += 1
            row += " " if n == 0 else ("." if n < 3 else "#")
        print("  %6.1f %s" % (z, row))

print("\n=== pocket skin: cover material above Z 1.8, per X, at Y 9..14 ===")
foot = m.build_foot()
m.STOP_ENABLE = False
cover = m.build_cover(outer, foot)
for x in (36.0, 44.2, 50.0, 56.0, 67.2, 80.0, 95.0):
    zs = []
    for z in [i * 0.1 for i in range(-40, 40)]:
        if cover.isInside(App.Vector(x, 11.0, z), 1e-6, True):
            zs.append(z)
    if zs:
        print("  X %5.1f : cover at Y 11 spans Z %.1f..%.1f" % (x, min(zs), max(zs)))
    else:
        print("  X %5.1f : cover at Y 11 -- none" % x)

print("\n=== how far DOWN does cover material reach, per X, in the hinge band? ===")
for x in (36.0, 44.2, 50.0, 56.0, 60.0, 67.2, 74.0, 80.0, 95.0, 100.0):
    best = None
    for y in [i * 0.25 for i in range(0, 60)]:
        for z in [i * 0.25 for i in range(-40, 20)]:
            if cover.isInside(App.Vector(x, y, z), 1e-6, True):
                if best is None or z < best[0]:
                    best = (z, y)
    print("  X %5.1f : lowest cover point Z %.2f at Y %.2f" % (x, best[0], best[1])
          if best else "  X %5.1f : none" % x)
