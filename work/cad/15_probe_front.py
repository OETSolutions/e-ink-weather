"""Diagnose the four front fasteners against the real front roll, and the foot pocket skin.

Questions:
 1. What is the bezel's local front surface Z at each fastener axis (the roll has already
    consumed material out there)?
 2. Does the boss cylinder protrude beyond that local surface (a visible nub with a hole)?
 3. How much skin is left over each blind pilot?
 4. How much rear-cover skin is left over the battery footprint, given the foot pocket?
"""
import FreeCAD as App
import Part
import os

HERE = os.path.dirname(os.path.abspath(__file__))
D = 16.9
FASTENERS = [(2.3, 32.0), (2.3, 76.5), (132.1, 32.0), (132.1, 76.5)]

cap = App.openDocument(os.path.join(HERE, "display_capture.FCStd"))
bezel = cap.getObject("PRINT_FRONT_BEZEL").Shape
asm = App.openDocument(os.path.join(HERE, "EInk_Weather_Display_Assembly.FCStd"))
cover = asm.getObject("PRINT_REAR_COVER").Shape
foot = asm.getObject("PRINT_SWING_FOOT").Shape

print("=== FRONT ROLL GEOMETRY (from the shell at a mid-Y slice) ===")
sd = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
shell = sd.getObject("CASE_SHELL").Shape
# Max Z of the shell as a function of X, sampled across a Y band away from the corners.
for x in [0.2, 0.6, 1.0, 1.5, 2.0, 2.3, 3.0, 3.4, 3.8, 4.2, 4.5, 5.0, 8.0]:
    band = Part.makeBox(0.1, 20.0, 20.0, App.Vector(x, 30.0, 0.0))
    v = shell.common(band)
    if v.Volume > 0:
        print("  shell at x=%.1f: max Z = %.3f" % (x, v.BoundBox.ZMax))
    else:
        print("  shell at x=%.1f: none" % x)

print()
print("=== BEZEL vs ROLL AT EACH FASTENER ===")
for x, y in FASTENERS:
    # Bezel's own front surface near this axis, sampled just OUTBOARD of the boss.
    probe = Part.makeBox(0.6, 0.6, 6.0, App.Vector(x, y - 0.3, 15.0))
    bv = bezel.common(probe)
    local_top = bv.BoundBox.ZMax if bv.Volume > 0 else float("nan")
    # Boss cylinder as built: r1.8 from split plane up to PILOT_TOP.
    boss = Part.makeCylinder(1.8, 16.5 - 15.2, App.Vector(x, y, 15.2))
    over = bezel.common(boss).BoundBox.ZMax if bezel.common(boss).Volume > 0 else float("nan")
    # Material actually present above z=16.0 on this axis.
    above = Part.makeCylinder(0.75, 1.0, App.Vector(x, y, 16.0))
    skin = bezel.common(above).Volume
    print("  fastener (%.1f, %.1f): local bezel top Z=%.3f  boss top Z=%.3f  skin above 16.0 = %.3f mm3"
          % (x, y, local_top, over, skin))

print()
print("=== BEZEL FRONT SURFACE PROFILE NEAR x=0 (mid-Y) ===")
for x in [0.3, 0.8, 1.3, 1.8, 2.3, 2.8, 3.3, 3.8, 4.3, 4.8]:
    col = Part.makeBox(0.2, 0.2, 6.0, App.Vector(x, 32.0, 15.0))
    v = bezel.common(col)
    print("  bezel x=%.1f: top Z = %.3f" % (x, v.BoundBox.ZMax if v.Volume > 0 else float("nan")))

print()
print("=== FOOT POCKET vs BATTERY FOOTPRINT (rear cover skin) ===")
BAT_X0, BAT_X1, BAT_Y0, BAT_Y1 = 9.0, 48.0, 18.0, 88.0
print("  foot bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f" %
      (foot.BoundBox.XMin, foot.BoundBox.XMax, foot.BoundBox.YMin, foot.BoundBox.YMax,
       foot.BoundBox.ZMin, foot.BoundBox.ZMax))
print("  cover bbox Z %.2f..%.2f" % (cover.BoundBox.ZMin, cover.BoundBox.ZMax))
# Sample the cover's remaining thickness on a grid over the battery footprint.
print("  remaining cover thickness over the battery footprint (mm):")
ys = [20, 28, 36, 44, 52, 60, 68, 76, 84]
xs = [11, 17, 23, 29, 35, 41, 47]
hdr = "     y\\x " + "".join("%6d" % v for v in xs)
print(hdr)
for y in ys:
    row = "  %5d " % y
    for x in xs:
        col = Part.makeCylinder(1.0, 8.0, App.Vector(x, y, -0.5))
        v = cover.common(col).Volume / (3.14159 * 1.0 * 1.0)
        row += "%6.2f" % v
    print(row)
print("  (a value of 0.00 means the foot pocket removed the cover completely there)")
