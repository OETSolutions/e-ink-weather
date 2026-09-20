"""What is actually there in the service hatch and the USB cable passage.

Prints ASCII material maps through the bay and the USB slot so every blocker is visible:
  C = cover material, H = hatch material, X = both, . = empty.

Env: SRC
"""
import importlib
import os
import sys

import FreeCAD as App

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
m = importlib.import_module("06_rear_cover_foot")

SRC = os.environ.get("SRC", "rear_cover_foot.FCStd")
doc = App.openDocument(SRC)
cover = doc.getObject("PRINT_REAR_COVER").Shape
hatch = doc.getObject("PRINT_SERVICE_HATCH").Shape


def mat(shape, x, y, z):
    return shape.isInside(App.Vector(x, y, z), 1e-6, True)


def amap(x, y0, y1, dy, z0, z1, dz, title):
    print("\n" + title + "   (slice at x=%.2f)" % x)
    yvals = []
    v = y0
    while v <= y1 + 1e-9:
        yvals.append(v)
        v += dy
    print("        y= " + "".join("%d" % (int(v) % 10) for v in yvals))
    z = z1
    while z >= z0 - 1e-9:
        row = ""
        for y in yvals:
            c = mat(cover, x, y, z)
            h = mat(hatch, x, y, z)
            row += "X" if (c and h) else ("C" if c else ("H" if h else "."))
        print("   z=%6.2f  %s" % (z, row))
        z -= dz


print("USB_SLOT_X %.2f  W %.2f   Y %.2f..%.2f   H %.2f"
      % (m.USB_SLOT_X, m.USB_SLOT_W, m.SERVICE_BAY_Y1 - 1.0, m.SERVICE_BAY_Y1 - 1.0 + 6.0,
         m.USB_SLOT_H))
print("SERVICE_BAY  X %.2f..%.2f  Y %.2f..%.2f"
      % (m.SERVICE_BAY_X0, m.SERVICE_BAY_X1, m.SERVICE_BAY_Y0, m.SERVICE_BAY_Y1))
print("FLANGE       X %.2f..%.2f  Y %.2f..%.2f  z %.2f..%.2f"
      % (m.HATCH_FLANGE_X0, m.HATCH_FLANGE_X1, m.HATCH_FLANGE_Y0, m.HATCH_FLANGE_Y1,
         -m.HATCH_FLANGE_T, m.HATCH_FLANGE_T))
print("SCALLOP      x %.2f r %.2f at Y %.2f" % (m.HATCH_SCALLOP_X, m.HATCH_SCALLOP_R,
                                                m.HATCH_FLANGE_Y1))
print("cover solids %d   hatch solids %d" % (len(cover.Solids), len(hatch.Solids)))

# Through the USB slot.
amap(m.USB_SLOT_X, 84.0, 100.0, 0.5, -8.0, 3.5, 0.5, "-- USB cable passage --")

# Through the bay centre.
amap(50.85, 64.0, 96.0, 0.75, -8.0, 3.5, 0.4, "-- bay centre: the plug --")

# Along the bay's -X edge, through key 1.
amap(21.3, 64.0, 96.0, 0.75, -4.0, 3.5, 0.4, "-- outside the bay -X edge: key 1 --")

# --- is the USB slot a THROUGH passage? ------------------------------------------
print("\n--- USB slot openness: walk y at the slot centre, z through the cover's depth ---")
blocked = []
for z in (2.5, 1.5, 0.5, -0.5, -1.5, -3.0, -5.0, -7.0):
    row = ""
    for yy in (88.0, 89.0, 90.0, 90.5, 91.0, 91.5, 92.0, 93.0, 94.0, 95.0, 96.0, 97.0, 98.0):
        c = mat(cover, m.USB_SLOT_X, yy, z)
        h = mat(hatch, m.USB_SLOT_X, yy, z)
        row += "X" if (c and h) else ("C" if c else ("H" if h else "."))
        if c:
            blocked.append((z, yy))
    print("  z=%6.2f  y88..98: %s" % (z, row))
print("cover material inside the slot footprint (x=%.1f, y 91..97): %d samples"
      % (m.USB_SLOT_X, sum(1 for z, yy in blocked if 91.0 <= yy <= 97.0)))

# --- hatch sub-feature outline ----------------------------------------------------
print("\n--- hatch material outline (top view), z=+1.5 through the plug ---")


def outline(shape, z, x0, x1, y0, y1, step):
    print("   z=%.2f" % z)
    xs = []
    v = x0
    while v <= x1 + 1e-9:
        xs.append(v)
        v += step
    print("     x= " + "".join("%d" % (int(v / 5) % 10) for v in xs))
    y = y1
    while y >= y0 - 1e-9:
        row = "".join("#" if mat(shape, x, y, z) else "." for x in xs)
        print("  y=%6.1f %s" % (y, row))
        y -= step


outline(hatch, 1.5, 14.0, 92.0, 64.0, 100.0, 1.5)
outline(hatch, -0.6, 14.0, 92.0, 64.0, 100.0, 1.5)
