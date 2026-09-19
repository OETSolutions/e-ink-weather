"""Constants + material map at a BETWEEN-clip station, + the descending assembly path."""
import os
import sys

import FreeCAD as App
import Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

print("COVER_T %.2f  FOOT_X %.2f FOOT_Y %.2f FOOT_W %.2f FOOT_H %.2f FOOT_T %.2f"
      % (m.COVER_T, m.FOOT_X, m.FOOT_Y, m.FOOT_W, m.FOOT_H, m.FOOT_T))
print("POCKET_DEPTH %.2f POCKET_CLEAR %.2f  WEB_Y0 %.2f WEB_Z1 %.2f"
      % (m.POCKET_DEPTH, m.POCKET_CLEAR, m.WEB_Y0, m.WEB_Z1))
print("PIN_R %.2f BORE_R %.2f CLIP_WALL %.2f CLIP_W %.2f PIN_ROOT %.2f"
      % (m.PIN_R, m.BORE_R, m.CLIP_WALL, m.CLIP_W, m.PIN_ROOT))
print("HINGE_Y %.2f KNUCKLE_Z %.2f  cavity_r %.2f"
      % (m.HINGE_Y, m.KNUCKLE_Z, m.BORE_R+m.CLIP_WALL+0.6))

doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = doc.getObject("CASE_SHELL").Shape
foot = m.build_foot()
m.STOP_ENABLE = False
cover_ns = m.build_cover(outer, foot)
m.STOP_ENABLE = True
cover = m.build_cover(outer, foot)

Y0, Y1, Z0, Z1, D = -2.0, 20.0, -9.0, 6.0, 0.5


def cell(shape, x, y, z):
    n = 0
    for dy in (0.16, 0.34):
        for dz in (0.16, 0.34):
            if shape.isInside(App.Vector(x, y + dy, z + dz), 1e-6, True):
                n += 1
    return n


def draw(title, shape, x):
    print("\n%s  (X %.2f)" % (title, x))
    print("      " + "".join("%d" % (abs(int(y)) % 10) for y in
                             [Y0 + i * D for i in range(int((Y1 - Y0) / D))]))
    for j in range(int((Z1 - Z0) / D)):
        z = Z0 + j * D
        row = ""
        for i in range(int((Y1 - Y0) / D)):
            y = Y0 + i * D
            n = cell(shape, x, y, z)
            row += " " if n == 0 else ("." if n < 3 else "#")
        print("%6.1f %s" % (z, row))


draw("COVER without stop, GAP station", cover_ns, 56.0)
draw("COVER without stop, CLIP station", cover_ns, 44.2)
draw("FOOT (folded), GAP station", foot, 56.0)

print("\n=== DESCENDING assembly path (foot lowered onto the pin from +Z) ===")
print("current mouth faces -Z, so this is the only radial path that exists")
CXS = m.ARM_CENTERS
for dz in (14, 12, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0):
    g = foot.copy().translate(App.Vector(0, 0, dz))
    inter = g.common(cover_ns)
    ib = ob = 0.0
    for s in inter.Solids:
        cx = (s.BoundBox.XMin + s.BoundBox.XMax) / 2.0
        if any(abs(cx - c) <= 1.7 for c in CXS):
            ib += s.Volume
        else:
            ob += s.Volume
    print("  dz %4d : clip-band %8.3f  elsewhere %8.3f" % (dz, ib, ob))
