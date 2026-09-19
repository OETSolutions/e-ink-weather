"""Cross-section of the hinge: what does the clip actually look like in Y-Z?

Prints an occupancy map of the foot and the cover in the clip-band plane, so the entry
geometry is visible instead of inferred.
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
foot = m.build_foot()

m.STOP_ENABLE = False
cover = m.build_cover(outer, foot)
m.STOP_ENABLE = True
cover_stop = m.build_cover(outer, foot)

CX = m.ARM_CENTERS[1]          # middle clip station, X 67.2
PIN_R = m.PIN_R
BORE_R = m.BORE_R
HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
print("station X %.2f  pin r %.2f  bore r %.2f  axis (Y %.2f, Z %.2f)"
      % (CX, PIN_R, BORE_R, HY, HZ))

Y0, Y1 = -6.0, 16.0
Z0, Z1 = -14.0, 8.0
DY = 0.5
DZ = 0.5


def cls(shape, p):
    if shape.isInside(p, 1e-6, True):
        return True
    return False


def cell(shape, y, z):
    """fraction of the cell occupied, sampled 2x2"""
    n = 0
    for dy in (0.16, 0.34):
        for dz in (0.16, 0.34):
            if cls(shape, App.Vector(CX, y + dy, z + dz)):
                n += 1
    return n


def draw(title, shape, mark="##"):
    print("\n%s" % title)
    print("      " + "".join("%d" % (abs(int(y)) % 10) for y in
                             [Y0 + i * DY for i in range(int((Y1 - Y0) / DY))]))
    for j in range(int((Z1 - Z0) / DZ)):
        z = Z0 + j * DZ
        row = ""
        for i in range(int((Y1 - Y0) / DY)):
            y = Y0 + i * DY
            n = cell(shape, y, z)
            row += " " if n == 0 else (mark[0] if n < 3 else mark)
        print("%6.1f %s" % (z, row))


draw("FOOT (one clip station, X %.1f)" % CX, foot)
draw("COVER (with stop)", cover_stop)

print("\n--- overlaid: 'F' foot only, 'C' cover only, '#' both ---")
print("      " + "".join("%d" % (abs(int(y)) % 10) for y in
                         [Y0 + i * DY for i in range(int((Y1 - Y0) / DY))]))
for j in range(int((Z1 - Z0) / DZ)):
    z = Z0 + j * DZ
    row = ""
    for i in range(int((Y1 - Y0) / DY)):
        y = Y0 + i * DY
        f = cell(foot, y, z) > 0
        c = cell(cover_stop, y, z) > 0
        row += "#" if (f and c) else ("F" if f else ("C" if c else " "))
    print("%6.1f %s" % (z, row))
