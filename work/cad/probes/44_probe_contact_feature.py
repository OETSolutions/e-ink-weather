"""Identify the exact foot feature that strikes the stop lug."""
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
foot = m.build_foot()

x0 = m.HINGE_X0 - m.PIN_ROOT
lug = Part.makeBox(6.0, m.STOP_LUG_Y, m.STOP_LUG_Z,
                   App.Vector(x0, m.STOP_LUG_Y0, m.STOP_LUG_Z0))

leg = foot.copy()
leg.rotate(HINGE, AXIS, -56.0)
inter = leg.common(lug)
print("intersection volume %.4f" % inter.Volume)
for s in inter.Solids:
    bb = s.BoundBox
    print("  solid vol %.4f  X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
    pts = sorted({(round(v.Point.x, 2), round(v.Point.y, 2), round(v.Point.z, 2))
                  for v in s.Vertexes})
    for p in pts:
        print("      v %.2f %.2f %.2f" % p)

# Un-rotate the intersection points to see the foot's own coordinates.
print("\nun-rotated (foot frame):")
for s in inter.Solids:
    for v in s.Vertexes:
        p = App.Vector(v.Point)
        # inverse of rotate(center,axis,-56): rotate by +56
        d = p - HINGE
        a = __import__("math").radians(56.0)
        ny = HINGE.y + d.y * __import__("math").cos(a) - d.z * __import__("math").sin(a)
        nz = HINGE.z + d.y * __import__("math").sin(a) + d.z * __import__("math").cos(a)
        print("   x %.2f  y %.2f  z %.2f" % (p.x, ny, nz))
