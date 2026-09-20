"""Fine material map of the USB cable slot, and a connectivity test through it.

Env: SRC
"""
import importlib
import os
import sys

import FreeCAD as App

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.environ.get("SRC", "rear_cover_foot.FCStd"))
cover = doc.getObject("PRINT_REAR_COVER").Shape
hatch = doc.getObject("PRINT_SERVICE_HATCH").Shape


def c(x, y, z):
    p = App.Vector(x, y, z)
    return cover.isInside(p, 1e-6, True)


def h(x, y, z):
    p = App.Vector(x, y, z)
    return hatch.isInside(p, 1e-6, True)


X0, X1 = 22.0, 37.0
Y0, Y1 = 88.0, 100.0
DX, DY = 0.5, 0.25

print("slot X %.2f..%.2f  Y %.2f..%.2f  z -0.2..3.2"
      % (m.USB_SLOT_X - m.USB_SLOT_W / 2, m.USB_SLOT_X + m.USB_SLOT_W / 2,
         m.SERVICE_BAY_Y1 - 1.0, m.SERVICE_BAY_Y1 + 5.0))
print("C=cover H=hatch X=both .=empty")
for z in (3.0, 2.6, 2.0, 1.4, 0.8, 0.2, -0.2, -0.6, -1.0, -1.4, -1.8, -2.5, -4.0):
    xs = []
    v = X0
    while v <= X1 + 1e-9:
        xs.append(v)
        v += DX
    hdr = "     x= " + "".join("%d" % (int(v) % 10) for v in xs)
    print("\n z=%5.2f" % z)
    print(hdr)
    y = Y1
    while y >= Y0 - 1e-9:
        row = ""
        for x in xs:
            cc = c(x, y, z)
            hh = h(x, y, z)
            row += "X" if (cc and hh) else ("C" if cc else ("H" if hh else "."))
        print("  y=%6.2f %s" % (y, row))
        y -= DY

print("\n--- connectivity: is there an empty path along Y through the cover at the slot? ---")
for x in (26.0, 29.6, 33.0, 25.5, 34.0):
    for z in (2.8, 2.0, 1.0, 0.0, -0.6, -1.0, -1.5, -2.5, -4.0, -6.0):
        blocked = [y for y in [88 + 0.25 * i for i in range(0, 49)]
                   if c(x, y, z) or h(x, y, z)]
        # the slot band is y 91..97; anything blocking inside it means closed
        inband = [y for y in blocked if 91.0 <= y <= 97.0]
        print("  x=%5.2f z=%5.2f  blockers in slot band y91..97: %s"
              % (x, z, ("%.2f..%.2f (%d)" % (min(inband), max(inband), len(inband)))
                 if inband else "NONE"))
    print()
