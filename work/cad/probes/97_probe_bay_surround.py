"""Map cover material around the service bay, at the depths a rebate would occupy.

The hatch's anti-rotation lip (59.80 x 27.10) is 3.10 mm LARGER than the bay opening
(56.70 x 24.00), so it can never pass through the hole -- the user's "you can't put the door
in". The only way a feature bigger than the opening can get behind the cover is through a
rebate that OPENS TO THE EXTERIOR FACE.

Before designing that rebate, find out what cover material actually exists in the band it
would occupy and under the flange lap.
"""
import os
import sys

import FreeCAD as App
import Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = src.getObject("CASE_SHELL").Shape
m.STOP_ENABLE = False
cover = m.build_cover(outer, m.build_foot())
m.STOP_ENABLE = True

X0, X1 = m.SERVICE_BAY_X0, m.SERVICE_BAY_X1
Y0, Y1 = m.SERVICE_BAY_Y0, m.SERVICE_BAY_Y1
ROX0, ROY0, ROX1, ROY1 = m.HATCH_LIP_OUT
print("bay        X %.2f..%.2f  Y %.2f..%.2f  (%.2f x %.2f)" % (X0, X1, Y0, Y1, X1 - X0, Y1 - Y0))
print("lip/recess X %.2f..%.2f  Y %.2f..%.2f  (%.2f x %.2f)"
      % (ROX0, ROX1, ROY0, ROY1, ROX1 - ROX0, ROY1 - ROY0))
print("keyhole slots X 53.40..57.40 & 77.00..81.00  Y 57.00..65.00")
print("cover USB slot X 21.40..30.40  Y 91.00..97.00")

print("\n=== cover occupancy on a grid, by Z (rows = Y, cols = X) ===")
xs = [19.5, 21.5, 23.5, 30.0, 50.0, 78.0, 80.0, 82.0, 84.5, 88.0, 92.0]
ys = [65.0, 65.5, 66.0, 66.6, 67.5, 69.0, 80.0, 91.0, 92.5, 93.2, 94.0, 95.0]
for z in (0.3, 1.0, 1.6, 2.0, 2.9):
    print("\n  z = %.1f" % z)
    print("        " + "".join("%6.1f" % x for x in xs))
    for y in ys:
        row = "  y %5.1f" % y
        for x in xs:
            row += "  %4s" % ("#" if cover.isInside(App.Vector(x, y, z), 1e-6, True) else ".")
        print(row)

print("\n=== does a rebate outer = HATCH_LIP_OUT leave a floor? ===")
print("   cut band z -0.2..1.60 -> cover material must remain at z 1.60..3.00")
for x, y, lab in [(21.5, 80.0, "-X band"), (50.0, 66.8, "-Y band"), (50.0, 93.2, "+Y band"),
                  (80.0, 80.0, "+X band")]:
    col = cover.common(Part.makeBox(0.02, 0.02, 10, App.Vector(x, y, -1)))
    bb = col.BoundBox
    print("   %-9s (%5.1f,%5.1f): Z %.2f..%.2f  vol %.4f"
          % (lab, x, y, bb.ZMin, bb.ZMax, col.Volume))

print("\n=== flange lap bands: is there solid cover at z=0 to land on? ===")
for lab, y in [("-Y lap Y0 64.85", 64.85), ("-Y lap Y0 65.25", 65.25), ("+Y lap Y1 95.15", 95.15)]:
    for x in (25.0, 40.0, 55.4, 67.2, 79.0, 82.0, 86.0):
        p = App.Vector(x, y, 0.15)
        print("   %-18s x %5.1f : %s" % (lab, x, "#" if cover.isInside(p, 1e-6, True) else "."))
