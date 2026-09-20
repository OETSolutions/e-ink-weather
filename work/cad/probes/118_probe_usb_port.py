"""Where the board's USB-C port actually is, and whether a cable can reach it.

Env: (none)
"""
import os

import FreeCAD as App

HERE = os.path.dirname(os.path.abspath(__file__))
doc = App.openDocument(os.path.join(os.path.dirname(HERE), "esp32_m1.FCStd"))
for n in ("CLR_USB_C", "P2_USB_C", "P2_USB_C_Port"):
    o = doc.getObject(n)
    if o:
        print("%-16s %s" % (n, o.Shape.BoundBox))
        if o.Shape.Solids:
            print("                 vol %.3f" % o.Shape.Volume)

# the transform used by the chassis
BOARD_TX, BOARD_TY, BOARD_Z = None, None, None
src = open(os.path.join(HERE, "09_main_chassis_final.py")).read()
import re
for k in ("BOARD_TX", "BOARD_TY", "BOARD_Z"):
    mm = re.search(r"^%s\s*=\s*([-\d.]+)" % k, src, re.M)
    if mm:
        print(k, "=", mm.group(1))

asm = App.openDocument(os.path.join(HERE, "EInk_Weather_Display_Assembly.FCStd"))
for n in ("BOARD_P2_USB_C", "BOARD_P2_USB_C_Port", "PRINT_REAR_COVER",
          "PRINT_SERVICE_HATCH", "BOARD_PCB_48x66x1"):
    o = asm.getObject(n)
    if o and hasattr(o, "Shape") and not o.Shape.isNull():
        print("%-24s %s" % (n, o.Shape.BoundBox))
