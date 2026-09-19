"""Overlay map at the moment the clips are being pushed on, to see exactly what blocks."""
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
foot0 = m.build_foot()
cover = m.build_cover(outer, foot0)

CX = m.ARM_CENTERS[1]
Y0, Y1, Z0, Z1, D = -6.0, 16.0, -12.0, 6.0, 0.5


def cell(shape, y, z):
    n = 0
    for dy in (0.16, 0.34):
        for dz in (0.16, 0.34):
            if shape.isInside(App.Vector(CX, y + dy, z + dz), 1e-6, True):
                n += 1
    return n


def draw(dz):
    foot = foot0.copy().translate(App.Vector(0, 0, dz))
    print("\n=== foot at dz %+.1f : F foot, C cover, # both, o neither ===" % dz)
    print("      " + "".join("%d" % (abs(int(y)) % 10) for y in
                             [Y0 + i * D for i in range(int((Y1 - Y0) / D))]))
    for j in range(int((Z1 - Z0) / D)):
        z = Z0 + j * D
        row = ""
        for i in range(int((Y1 - Y0) / D)):
            y = Y0 + i * D
            f = cell(foot, y, z) > 0
            c = cell(cover, y, z) > 0
            row += "#" if (f and c) else ("F" if f else ("C" if c else " "))
        print("%6.1f %s" % (z, row))


for dz in (-6.0, -4.0, -2.0, 0.0):
    draw(dz)
