"""Openness map: where can a cable actually pass through the rear cover + hatch?

Point sampling can miss a thin blocker and shaded renders can hide one. This casts a RAY
along +Z through the rear cover and the hatch at every (x,y) in the service-bay region and
reports whether the column is open through both. Open = white, blocked = grey, in a PNG.

Also checks the CHASSIS service wall for the hole that faces the board's USB-C port.

Env: SRC
"""
import os

import FreeCAD as App
import numpy as np

SRC = os.environ.get("SRC", "rear_cover_foot.FCStd")
RSCALE = int(os.environ.get("RSCALE", "12"))
OUT = os.environ.get("OUT", "/tmp/openmap.ppm")

doc = App.openDocument(SRC)
cover = doc.getObject("PRINT_REAR_COVER").Shape
hatch = doc.getObject("PRINT_SERVICE_HATCH").Shape

asm = App.openDocument("/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/"
                       "work/cad/EInk_Weather_Display_Assembly.FCStd")
chassis = asm.getObject("PRINT_REAR_CHASSIS").Shape
port = asm.getObject("BOARD_P2_USB_C_Port").Shape
print("port bbox", port.BoundBox)

pb = port.BoundBox
chassis_in_port = chassis.common(
    App.ActiveDocument.addObject("Part::Box", "_t")).Volume if False else None
import Part
portbox = Part.makeBox(pb.XLength, pb.YLength, pb.ZLength, App.Vector(pb.XMin, pb.YMin, pb.ZMin))
print("chassis material inside the port's projected box: %.4f mm3 (0 = chassis has a hole)"
      % chassis.common(portbox).Volume)
# and just behind the port
behind = Part.makeBox(pb.XLength, 2.0, pb.ZLength, App.Vector(pb.XMin, pb.YMax, pb.ZMin))
print("chassis material in the 2 mm slab just outside the port: %.4f mm3"
      % chassis.common(behind).Volume)

x0, x1 = 14.0, 92.0
y0, y1 = 62.0, 101.0
W = int((x1 - x0) * RSCALE)
H = int((y1 - y0) * RSCALE)
img = np.full((H, W, 3), 1.0, dtype=np.float32)

zs = np.arange(-2.0, 3.6, 0.1)
open_xy = np.zeros((H, W), dtype=bool)
bay = np.zeros((H, W), dtype=bool)
for j in range(H):
    y = y0 + (j + 0.5) / RSCALE
    for i in range(W):
        x = x0 + (i + 0.5) / RSCALE
        cov = False
        hat = False
        for z in zs:
            p = App.Vector(x, y, z)
            if cover.isInside(p, 1e-7, True):
                cov = True
            if hatch.isInside(p, 1e-7, True):
                hat = True
        open_xy[j, i] = not (cov or hat)
        bay[j, i] = (not cov) or (not hat)

img[~open_xy] = (0.80, 0.80, 0.82)
img[open_xy] = (0.10, 0.12, 0.15)

# mark the USB slot's designed rectangle
import importlib
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
m = importlib.import_module("06_rear_cover_foot")
sx0 = m.USB_SLOT_X - m.USB_SLOT_W / 2
sy0 = m.SERVICE_BAY_Y1 - 1.0
def stamp(xa, xb, ya, yb, col):
    ia = int((xa - x0) * RSCALE); ib = int((xb - x0) * RSCALE)
    ja = int((ya - y0) * RSCALE); jb = int((yb - y0) * RSCALE)
    for i2 in range(max(ia, 0), min(ib, W)):
        for edge_j in (ja, jb - 1):
            if 0 <= edge_j < H:
                img[edge_j, i2] = col
    for j2 in range(max(ja, 0), min(jb, H)):
        for edge_i in (ia, ib - 1):
            if 0 <= edge_i < W:
                img[j2, edge_i] = col
stamp(sx0, sx0 + m.USB_SLOT_W, sy0, sy0 + 6.0, (0.95, 0.15, 0.15))
stamp(m.SERVICE_BAY_X0, m.SERVICE_BAY_X1, m.SERVICE_BAY_Y0, m.SERVICE_BAY_Y1, (0.15, 0.35, 0.95))

a = (img * 255).astype(np.uint8)
with open(OUT, "wb") as f:
    f.write(b"P6\n%d %d\n255\n" % (W, H))
    f.write(a.tobytes())
print("wrote", OUT, "%dx%d  x %.1f..%.1f  y %.1f..%.1f  (black=open through, grey=blocked)"
      % (W, H, x0, x1, y0, y1))
print("red = USB slot design rect; blue = service bay design rect")

# how much of the slot rectangle is open?
n = 0
tot = 0
for j in range(H):
    y = y0 + (j + 0.5) / RSCALE
    if not (sy0 < y < sy0 + 6.0):
        continue
    for i in range(W):
        x = x0 + (i + 0.5) / RSCALE
        if sx0 < x < sx0 + m.USB_SLOT_W:
            tot += 1
            n += int(open_xy[j, i])
print("open fraction of the USB slot rectangle: %d/%d = %.1f%%" % (n, tot, 100.0 * n / max(tot, 1)))
