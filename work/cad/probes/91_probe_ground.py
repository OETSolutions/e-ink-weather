"""Does the stop hit the ground when the case is standing on the open leg?

Poses the case so the leg's tip is on z=0 and reports the lowest point of the cover (with the
stop) and of the leg. A stop that reached the ground would make the stand rock.
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
m.STOP_ENABLE = True
cover = m.build_cover(outer, foot)
m.STOP_ENABLE = False
cover_off = m.build_cover(outer, foot)
m.STOP_ENABLE = True

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
OPEN = m.SWING_DEG

leg = foot.copy()
leg.rotate(HINGE, AXIS, -OPEN)
print("leg open %.0f deg: bbox Y %.2f..%.2f Z %.2f..%.2f"
      % (OPEN, leg.BoundBox.YMin, leg.BoundBox.YMax,
         leg.BoundBox.ZMin, leg.BoundBox.ZMax))

# Tip = the leg's lowest point; find it and use it as the ground contact.
tipz = leg.BoundBox.ZMin
print("leg lowest Z = %.2f (this is the ground contact)" % tipz)

print("\ncase leaning back so the leg's tip sits on z=0:")
print("  lean(deg)  cover minZ   stop minZ    clear of ground?")
for lean in (20, 24, 27.6, 30, 35):
    a = math.radians(lean)
    # Lean the case back about the hinge axis: rotate by -lean about X (same sense as the leg).
    cov = cover.copy()
    cov.rotate(HINGE, AXIS, -float(lean))
    lg = leg.copy()
    lg.rotate(HINGE, AXIS, -float(lean))
    # Drop everything so the leg's tip is at z=0.
    drop = -lg.BoundBox.ZMin
    cov.translate(App.Vector(0, 0, drop))
    lg.translate(App.Vector(0, 0, drop))
    co = cover_off.copy()
    co.rotate(HINGE, AXIS, -float(lean))
    co.translate(App.Vector(0, 0, drop))
    print("   %5.1f     %8.2f  %8.2f (no-stop %.2f)   %s"
          % (lean, cov.BoundBox.ZMin, cov.BoundBox.ZMin - co.BoundBox.ZMin,
             co.BoundBox.ZMin,
             "stop is the lowest" if cov.BoundBox.ZMin < co.BoundBox.ZMin - 0.01 else "cover edge"))

print("\nlowest cover point when the leg is FOLDED and the case lies on its rear face:")
print("  cover minZ = %.2f  (rear cover outer face is z=0)" % cover.BoundBox.ZMin)
print("  foot  minZ = %.2f" % foot.BoundBox.ZMin)
