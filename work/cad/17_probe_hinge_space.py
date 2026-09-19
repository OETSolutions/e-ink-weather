"""How much free space exists at the hinge band, above the cover's inner face?

The rear cover is only 2.8 mm thick, which cannot house a full cylindrical pivot. But at
x >= 50 the case cavity above the cover is empty (the battery stops at x=48). This probe
maps the free Z range so a printed knuckle can legally project into the cavity.

Reports, on a grid over the hinge band, the Z interval that is free of chassis, board,
battery and panel.
"""
import FreeCAD as App
import Part
import os

HERE = os.path.dirname(os.path.abspath(__file__))
d = App.openDocument(os.path.join(HERE, "EInk_Weather_Display_Assembly.FCStd"))

CH = d.getObject("PRINT_REAR_CHASSIS").Shape
obstacles = [("chassis", CH)]
for n in ["BOARD_PCB_48x66x1", "BATTERY_70x39x11", "PANEL_GLASS"]:
    o = d.getObject(n)
    if o:
        obstacles.append((n, o.Shape))
bo = d.getObject("BOARD_U5_ESP32_WROOM_32D") or d.getObject("BOARD_U5_ESP32_WROOM_32")
if bo:
    obstacles.append(("wroom", bo.Shape))

Z0, Z1 = 2.8, 14.0
STEP = 0.25
print("Free Z above the cover inner face (z=%.1f). 'free' = no obstacle in a 0.4 mm column." % Z0)
xs = [34, 40, 46, 52, 58, 64, 70, 76, 82, 88, 94, 100]
print("     Y |" + "".join("%7d" % x for x in xs))
for y in [60, 62, 63, 64, 65, 66, 68, 70]:
    row = "  %5d |" % y
    for x in xs:
        free_top = None
        z = Z0
        while z < Z1:
            col = Part.makeCylinder(0.2, STEP + 0.01, App.Vector(x, y, z))
            hit = any(s.common(col).Volume > 1e-4 for _, s in obstacles)
            if hit:
                break
            free_top = z + STEP
            z += STEP
        row += "%7s" % ("--" if free_top is None else "%.1f" % free_top)
    print(row)
print()
print("(value = highest Z still free, starting from 2.8; '--' = blocked immediately)")
print()

print("=== WHAT SITS IN THE HINGE BAND (y 62..72, x 50..102) ===")
for name, s in obstacles:
    band = Part.makeBox(52.0, 10.0, 11.2, App.Vector(50.0, 62.0, 2.8))
    v = s.common(band)
    if v.Volume > 1e-3:
        bb = v.BoundBox
        print("  %-10s vol %9.2f  X %6.2f..%6.2f  Y %6.2f..%6.2f  Z %5.2f..%5.2f"
              % (name, v.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
print()
print("=== COVER INNER FACE AND CHASSIS BOTTOM IN THE BAND ===")
cov = d.getObject("PRINT_REAR_COVER").Shape
for x in [52, 70, 90, 100]:
    col = Part.makeCylinder(0.2, 12.0, App.Vector(x, 66.0, 2.0))
    cv = cov.common(col)
    ch = CH.common(col)
    print("  x=%3d: cover in col %8.3f (Z %s)   chassis %8.3f (Z %s)"
          % (x, cv.Volume,
             ("%.2f..%.2f" % (cv.BoundBox.ZMin, cv.BoundBox.ZMax)) if cv.Volume > 0 else "-",
             ch.Volume,
             ("%.2f..%.2f" % (ch.BoundBox.ZMin, ch.BoundBox.ZMax)) if ch.Volume > 0 else "-"))
