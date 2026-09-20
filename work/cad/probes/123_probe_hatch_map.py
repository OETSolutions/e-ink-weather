"""Definitive, LABELLED top-view map of the hatch: exact world coordinates, no render guessing.

Prints a character map of hatch material in the XY plane at each Z, with tick-labelled axes
and every feature's constant range overlaid, so there is no ambiguity about orientation.

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
hatch = doc.getObject("PRINT_SERVICE_HATCH").Shape
cover = doc.getObject("PRINT_REAR_COVER").Shape

X0, X1, DX = 12.0, 94.0, 1.0
Y0, Y1, DY = 64.0, 101.0, 1.0

xs = []
v = X0
while v <= X1 + 1e-9:
    xs.append(v)
    v += DX


def rowlabel(y):
    return "y=%5.1f" % y


def header():
    # tens digit of x for every cell
    print("        x " + "".join("%d" % (int(v / 10) % 10) for v in xs))
    print("          " + "".join("%d" % (int(v) % 10) for v in xs))


for z in (2.5, 1.0, -0.6):
    print("\n===== hatch at z=%.1f =====" % z)
    if z > 1.2:
        print("   (flange is z -1.2..1.2; at z=%.1f only the PLUG and KEYS exist)" % z)
    elif z >= 0.0:
        print("   (BOTH the flange z[-1.2..1.2] and the plug z[0..3.0] exist here)")
    else:
        print("   (below the plug's z=0 face: only the FLANGE exists)")
    header()
    y = Y1
    while y >= Y0 - 1e-9:
        row = "".join("#" if hatch.isInside(App.Vector(x, y, z), 1e-7, True) else "." for x in xs)
        print("  %s %s" % (rowlabel(y), row))
        y -= DY

print("\n===== key constants =====")
print("plug        x %.2f..%.2f  y %.2f..%.2f  z 0..%.2f"
      % (m.SERVICE_BAY_X0 + m.HATCH_CLEAR, m.SERVICE_BAY_X1 - m.HATCH_CLEAR,
         m.SERVICE_BAY_Y0 + m.HATCH_CLEAR, m.SERVICE_BAY_Y1 - m.HATCH_CLEAR, m.HATCH_PLUG_T))
print("flange      x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f"
      % (m.HATCH_FLANGE_X0, m.HATCH_FLANGE_X1, m.HATCH_FLANGE_Y0, m.HATCH_FLANGE_Y1,
         -m.HATCH_FLANGE_T, m.HATCH_FLANGE_T))
for i, (a, b, c, d) in enumerate(m.HATCH_KEYS, 1):
    print("key %d       x %.2f..%.2f  y %.2f..%.2f  z 0..%.2f" % (i, a, b, c, d, m.HATCH_KEY_T))
print("scallop     centre x %.2f  (y=%.2f)  r %.2f  -> bites to y %.2f"
      % (m.HATCH_SCALLOP_X, m.HATCH_FLANGE_Y1, m.HATCH_SCALLOP_R,
         m.HATCH_FLANGE_Y1 - m.HATCH_SCALLOP_R))
print("usb slot    x %.2f..%.2f  y %.2f..%.2f  z %.2f..%.2f"
      % (m.USB_SLOT_X - m.USB_SLOT_W / 2, m.USB_SLOT_X + m.USB_SLOT_W / 2,
         m.SERVICE_BAY_Y1 - 1.0, m.SERVICE_BAY_Y1 + 5.0, -m.HATCH_FLANGE_T - 0.3,
         m.HATCH_PLUG_T + 0.3))
print("screw hole  x=%.2f y=%.2f  d=%.2f" % (m.HATCH_SCREW_X, m.HATCH_SCREW_Y, m.HATCH_SCREW_D))

print("\n===== cover's bay + notches (for comparison) =====")
print("bay         x %.2f..%.2f  y %.2f..%.2f"
      % (m.SERVICE_BAY_X0, m.SERVICE_BAY_X1, m.SERVICE_BAY_Y0, m.SERVICE_BAY_Y1))
for i, (a, b, c, d) in enumerate(m.HATCH_KEYS, 1):
    print("notch %d     x %.2f..%.2f  y %.2f..%.2f"
          % (i, a - m.HATCH_KEY_CLEAR, b + m.HATCH_KEY_CLEAR,
             c - m.HATCH_KEY_CLEAR, d + m.HATCH_KEY_CLEAR))
print("usb slot    x %.2f..%.2f  y %.2f..%.2f (through cover)"
      % (m.USB_SLOT_X - m.USB_SLOT_W / 2, m.USB_SLOT_X + m.USB_SLOT_W / 2,
         m.SERVICE_BAY_Y1 - 1.0, m.SERVICE_BAY_Y1 + 5.0))

print("\n===== cover at z=1.5 (what the hatch sits against) =====")
header()
y = Y1
while y >= Y0 - 1e-9:
    row = "".join("#" if cover.isInside(App.Vector(x, y, 1.5), 1e-7, True) else "." for x in xs)
    print("  %s %s" % (rowlabel(y), row))
    y -= DY
