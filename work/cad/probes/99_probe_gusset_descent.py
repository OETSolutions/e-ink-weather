"""The new clip gussets must not block the foot's descent onto the cover's pin.

The gussets hang BELOW the plate's underside (down to Z -2.80), which is new material in the
region the foot sweeps through as it is lowered onto the pin. This is exactly the class of
failure the user has hit twice: a feature that measures fine as a welded solid but makes the
stand impossible to assemble. So drive the real foot down the real path and measure overlap.

Path (established in earlier rounds): the foot approaches from ABOVE (large +Z), mouth-first,
and is lowered until the clip bore seats on the pin at HINGE_Y/HINGLE_Z. Test the whole descent.
"""
import os
import sys

import FreeCAD as App
import Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
cover = d.getObject("PRINT_REAR_COVER").Shape
foot = d.getObject("PRINT_FOOT").Shape

print("seated foot/cover interference: %.4f mm3 (this IS the seated state)"
      % foot.common(cover).Volume)

print("\n=== descent path: foot lowered onto the pin, mouth first ===")
print("   dz is the height above the seated position")
worst = 0.0
for dz in [12, 10, 8, 6, 5, 4, 3, 2.5, 2.0, 1.5, 1.0, 0.5, 0.2, 0.0]:
    g = foot.copy()
    g.translate(App.Vector(0, 0, dz))
    ov = g.common(cover)
    # The seated interference (friction fit at the bore) is expected; anything ABOVE it means
    # the gusset has hit something on the way down.
    print("   dz %5.1f : %9.4f mm3  %s"
          % (dz, ov.Volume, "OK" if ov.Volume <= 7.7 else "<-- BLOCKED"))
    if dz > 0.5:
        worst = max(worst, ov.Volume)

print("\n   worst overlap during descent (dz > 0.5): %.4f mm3" % worst)
if worst > 7.8:
    print("   FAIL: something blocks the descent (was the reported defect class)")
else:
    print("   PASS: descent is clear; only the intended friction fit remains")

print("\n=== and the gussets must not foul the COVER when the leg swings open ===")
for deg in [0, 10, 20, 30, 40, 50, 60, 65]:
    g = foot.copy()
    if deg:
        g.rotate(App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0), -float(deg))
    ov = g.common(cover)
    offb = sum(s.Volume for s in ov.Solids
               if not any(abs((s.BoundBox.XMin + s.BoundBox.XMax) / 2 - c) <= 1.7
                          for c in m.ARM_CENTERS))
    print("   %3d deg : total %8.4f  off-bearing %8.4f mm3  %s"
          % (deg, ov.Volume, offb, "OK" if offb < 0.05 else "<-- BIND"))
