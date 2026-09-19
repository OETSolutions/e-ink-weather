"""Full assembly simulation: find the motion that actually puts the clips on the pin.

Also measures the stop's root so the "thin flange" claim can be quantified.

The mouth faces -Z (measured), so the pin must enter the clip travelling +Z. The foot is one
rigid part, so rotating the foot also swings the plate. This sweeps a family of paths.
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
HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
CXS = m.ARM_CENTERS

m.STOP_ENABLE = False
cover_nostop = m.build_cover(outer, foot)
m.STOP_ENABLE = True
cover = m.build_cover(outer, foot)
print("cover without stop %.1f mm3, with stop %.1f mm3 (ramps add %.1f)"
      % (cover_nostop.Volume, cover.Volume, cover.Volume - cover_nostop.Volume))


def split(inter):
    """(clip-band volume, elsewhere volume)."""
    ib = ob = 0.0
    for s in inter.Solids:
        cx = (s.BoundBox.XMin + s.BoundBox.XMax) / 2.0
        if any(abs(cx - c) <= 1.7 for c in CXS):
            ib += s.Volume
        else:
            ob += s.Volume
    return ib, ob


print("\n=== PATH A: radial snap, foot kept at its folded orientation ===")
print("mouth faces -Z, so the foot must rise in +Z; the plate moves with it.")
for dz in (-12, -10, -8, -6, -5, -4, -3, -2, -1, 0):
    g = foot.copy().translate(App.Vector(0, 0, dz))
    ib, ob = split(g.common(cover))
    print("  dz %4d : clip-band %8.3f  elsewhere %8.3f  %s"
          % (dz, ib, ob, "OK" if ob < 0.05 else "BLOCKED (cover body)"))

print("\n=== PATH B: snap at 90 deg open, then rotate closed ===")
print("(fit the clips with the plate hanging behind the case, then swing it in)")
for deg in (90, 80, 70, 65, 60, 50, 40, 30, 20, 10, 0):
    g = foot.copy()
    if deg:
        g.rotate(HINGE, AXIS, -float(deg))
    ib, ob = split(g.common(cover))
    print("  %3d deg : clip-band %8.3f  elsewhere %8.3f  %s"
          % (deg, ib, ob, "OK" if ob < 0.05 else "BLOCKED (cover body)"))

print("\n=== PATH C: rotate in from beyond 65 deg, riding over the 65 deg line ===")
for deg in (65, 70, 75, 80, 85, 90, 95, 100, 110, 120):
    g = foot.copy().rotate(HINGE, AXIS, -float(deg))
    ib, ob = split(g.common(cover))
    print("  %3d deg : clip-band %8.3f  elsewhere %8.3f  %s"
          % (deg, ib, ob, "OK" if ob < 0.05 else "BLOCKED (stop)"))

print("\n=== WHERE IS THE ASSEMBLY BLOCK? (path A, dz -4) ===")
g = foot.copy().translate(App.Vector(0, 0, -4))
inter = g.common(cover)
for s in sorted(inter.Solids, key=lambda x: -x.Volume)[:8]:
    bb = s.BoundBox
    print("   %8.3f mm3  X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
