"""Profile of the outer shell's bottom edge along Y, at several X."""
import os
import sys

import FreeCAD as App

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = doc.getObject("CASE_SHELL").Shape

print("outer bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
      % (outer.BoundBox.XMin, outer.BoundBox.XMax, outer.BoundBox.YMin,
         outer.BoundBox.YMax, outer.BoundBox.ZMin, outer.BoundBox.ZMax))

print("\nlowest Z of the shell, per Y, at X = 30 / 56 / 100")
print("   Y     min-Z @X30   @X56    @X100")
for yi in range(0, 30):
    y = yi * 1.0
    row = "  %5.1f " % y
    for x in (30.0, 56.0, 100.0):
        low = None
        for zi in range(-60, 60):
            z = zi * 0.25
            if outer.isInside(App.Vector(x, y, z), 1e-6, True):
                low = z
                break
        row += "  %8s" % ("--" if low is None else "%.2f" % low)
    print(row)

print("\nlowest Z of the shell, per X at Y = 3 / 5 / 7 / 9")
print("   X     min-Z @Y3    @Y5     @Y7     @Y9")
for xi in range(24, 45):
    x = xi * 3.0
    row = "  %5.1f " % x
    for y in (3.0, 5.0, 7.0, 9.0):
        low = None
        for zi in range(-60, 60):
            z = zi * 0.25
            if outer.isInside(App.Vector(x, y, z), 1e-6, True):
                low = z
                break
        row += "  %8s" % ("--" if low is None else "%.2f" % low)
    print(row)
