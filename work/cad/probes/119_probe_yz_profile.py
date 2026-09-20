"""YZ side profile at the USB slot: what does the case look like in section?

Env: SRC
"""
import os

import FreeCAD as App

doc = App.openDocument(os.environ.get("SRC", "rear_cover_foot.FCStd"))
cover = doc.getObject("PRINT_REAR_COVER").Shape
hatch = doc.getObject("PRINT_SERVICE_HATCH").Shape

asm = App.openDocument("/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/"
                       "work/cad/EInk_Weather_Display_Assembly.FCStd")
chassis = asm.getObject("PRINT_REAR_CHASSIS").Shape
board = asm.getObject("BOARD_PCB_48x66x1").Shape


def cells(x, y0, y1, dy, z0, z1, dz):
    print("      y: " + "".join("%d" % (int(v) % 10) for v in
                                [y0 + dy * i for i in range(int((y1 - y0) / dy) + 1)]))
    z = z1
    while z >= z0 - 1e-9:
        row = ""
        y = y0
        while y <= y1 + 1e-9:
            p = App.Vector(x, y, z)
            if chassis.isInside(p, 1e-6, True):
                c = "B"     # chassis (body)
            elif board.isInside(p, 1e-6, True):
                c = "D"
            elif cover.isInside(p, 1e-6, True):
                c = "C"
            elif hatch.isInside(p, 1e-6, True):
                c = "H"
            else:
                c = "."
            row += c
            y += dy
        print("  z=%6.2f %s" % (z, row))
        z -= dz


print("=== YZ profile at x=29.60 (through the USB slot) ===")
print("B=chassis D=board C=cover H=hatch .=empty   y %.0f..%.0f  z %.1f..%.1f"
      % (58, 106, -8, 14))
cells(29.60, 58, 106, 1.0, 14, -8, 0.5)
