"""Is the cable passthrough actually open, and can a USB-C plug reach the port?

Three independent tests, because point-sampling can miss a thin blocker:
  1. BOOLEAN: (cover U hatch) intersect a box exactly filling the slot passage. Must be 0.
  2. RASTER: the free cross-section of the passage at the plate's mid-depth.
  3. PATH: walk a USB-C-plug-sized solid from outside to the board's port and report the
     first blocker, including the CHASSIS and the BOARD (which the earlier probes omitted).

Env: SRC
"""
import importlib
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.environ.get("SRC", "rear_cover_foot.FCStd"))
cover = doc.getObject("PRINT_REAR_COVER").Shape
hatch = doc.getObject("PRINT_SERVICE_HATCH").Shape

asm = App.openDocument("/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/"
                       "work/cad/EInk_Weather_Display_Assembly.FCStd")
chassis = asm.getObject("PRINT_REAR_CHASSIS").Shape
board = asm.getObject("BOARD_PCB_48x66x1").Shape
for n in ("BOARD_P2_USB_C", "BOARD_P2_USB_C_Port"):
    o = asm.getObject(n)
    if o:
        print("%-20s bbox %s" % (n, o.Shape.BoundBox))

SX0 = m.USB_SLOT_X - m.USB_SLOT_W / 2
SY0 = m.SERVICE_BAY_Y1 - 1.0
SZ0 = -0.2
slot_box = Part.makeBox(m.USB_SLOT_W, 6.0, m.USB_SLOT_H,
                        App.Vector(SX0, SY0, SZ0))
print("\nslot passage box: x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f"
      % (SX0, SX0 + m.USB_SLOT_W, SY0, SY0 + 6.0, SZ0, SZ0 + m.USB_SLOT_H))

print("\n1. BOOLEAN (cover U hatch) inside the slot passage:")
print("   cover  intersection %.6f mm3" % cover.common(slot_box).Volume)
print("   hatch  intersection %.6f mm3" % hatch.common(slot_box).Volume)

print("\n2. RASTER free section at the plate mid-depth (z=1.5), x %.1f..%.1f, y %.1f..%.1f:"
      % (SX0 - 3, SX0 + m.USB_SLOT_W + 3, SY0 - 3, SY0 + 9))
print("     C=cover H=hatch X=both .=FREE")
xs = [SX0 - 3 + 0.5 * i for i in range(int((m.USB_SLOT_W + 6) / 0.5) + 1)]
print("     x= " + "".join("%d" % (int(v) % 10) for v in xs))
y = SY0 + 9.0
while y >= SY0 - 3.0 - 1e-9:
    row = ""
    for x in xs:
        p = App.Vector(x, y, 1.5)
        c = cover.isInside(p, 1e-7, True)
        h = hatch.isInside(p, 1e-7, True)
        row += "X" if (c and h) else ("C" if c else ("H" if h else "."))
    print("   y=%6.2f %s" % (y, row))
    y -= 0.5

print("\n3. PATH: USB-C plug body (8.4 x 2.8 x 12) stepped from outside to the port.")
PW, PH, PL = 8.4, 2.8, 12.0
PX = m.USB_SLOT_X
PZ = 9.66                          # the port's centre, from BOARD_P2_USB_C_Port
print("   plug axis along Y at x=%.2f z=%.2f, body y[Yc-6..Yc+6]" % (PX, PZ))
print("     Yc   | chassis | board | cover | hatch | verdict")
Yc = 100.0
while Yc >= 66.0:
    plug = Part.makeBox(PW, PL, PH, App.Vector(PX - PW / 2, Yc - PL / 2, PZ - PH / 2))
    vc = chassis.common(plug).Volume
    vb = board.common(plug).Volume
    vco = cover.common(plug).Volume
    vh = hatch.common(plug).Volume
    verdict = "ok" if max(vc, vb, vco, vh) < 1e-6 else "BLOCKED"
    if verdict != "ok" or Yc in (100.0, 94.0, 90.0, 80.0, 72.0, 70.0, 69.0):
        print("   %6.2f | %7.3f | %5.3f | %5.3f | %5.3f | %s"
              % (Yc, vc, vb, vco, vh, verdict))
    Yc -= 1.0
