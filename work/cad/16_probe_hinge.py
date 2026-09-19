"""Measure the existing hinge: barrel spans, pin, clearances, and foot flushness.

The hinge currently needs a purchased 2.0 mm metal pin. This probe establishes the exact
envelope an integral printed joint has to fit into.
"""
import FreeCAD as App
import Part
import os

HERE = os.path.dirname(os.path.abspath(__file__))
d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
cover = d.getObject("PRINT_REAR_COVER").Shape
foot = d.getObject("PRINT_FOOT").Shape

CASE_W, CASE_H, COVER_T = 134.4, 108.5, 2.8
FOOT_W, FOOT_H, FOOT_T = 70.0, 40.0, 1.8
FOOT_X, FOOT_Y = (CASE_W - FOOT_W) / 2, 25.0
HINGE_Y, HINGE_Z = FOOT_Y + FOOT_H, 2.0
POCKET_CLEAR, POCKET_DEPTH = 0.30, 1.8

print("=== NOMINAL ===")
print("  foot pocket   : X %.1f..%.1f  Y %.1f..%.1f  depth %.2f" %
      (FOOT_X - POCKET_CLEAR, FOOT_X + FOOT_W + POCKET_CLEAR,
       FOOT_Y - POCKET_CLEAR, FOOT_Y + FOOT_H + POCKET_CLEAR, POCKET_DEPTH))
print("  foot          : X %.1f..%.1f  Y %.1f..%.1f  t %.2f" %
      (FOOT_X, FOOT_X + FOOT_W, FOOT_Y, FOOT_Y + FOOT_H, FOOT_T))
print("  hinge axis    : Y %.2f  Z %.2f  (X-parallel)" % (HINGE_Y, HINGE_Z))
print("  cover thickness %.2f ; pocket skin %.2f" % (COVER_T, COVER_T - POCKET_DEPTH))

print()
print("=== ACTUAL SOLID EXTENTS ===")
cb, fb = cover.BoundBox, foot.BoundBox
print("  cover bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f" %
      (cb.XMin, cb.XMax, cb.YMin, cb.YMax, cb.ZMin, cb.ZMax))
print("  foot  bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f" %
      (fb.XMin, fb.XMax, fb.YMin, fb.YMax, fb.ZMin, fb.ZMax))

print()
print("=== FLUSHNESS: foot outer face vs cover outer face ===")
# Cover outer surface Z at a point inside the pocket footprint but outside the foot.
for (x, y) in [(34.0, 30.0), (100.0, 30.0), (34.0, 62.0), (67.0, 45.0)]:
    probe = Part.makeCylinder(0.4, 8.0, App.Vector(x, y, -0.5))
    cv, fv = cover.common(probe).Volume, foot.common(probe).Volume
    cz = cover.common(probe).BoundBox.ZMax if cv > 0 else float("nan")
    fz = foot.common(probe).BoundBox.ZMax if fv > 0 else float("nan")
    print("  (%.0f,%.0f) cover top Z=%.3f  foot top Z=%.3f" % (x, y, cz, fz))

print()
print("=== HINGE MATERIAL ALONG THE AXIS (X scan at Y=%.1f, Z=%.1f) ===" % (HINGE_Y, HINGE_Z))
prev = None
runs = []
for i in range(0, 1350):
    x = i * 0.1
    probe = Part.makeCylinder(0.35, 34.0, App.Vector(x, HINGE_Y, HINGE_Z - 17.0))
    has_c = cover.common(probe).Volume > 1e-4
    has_f = foot.common(probe).Volume > 1e-4
    tag = ("C" if has_c else "-") + ("F" if has_f else "-")
    if tag != prev:
        runs.append([tag, x, x])
        prev = tag
    else:
        runs[-1][2] = x
for tag, a, b in runs:
    print("  %s  X %6.1f .. %6.1f  (len %5.1f)" % (tag, a, b, b - a))
print("  legend: C=cover present, F=foot present, CF=both (interference), '-'=nothing")

print()
print("=== RADIAL CLEARANCE BETWEEN FOOT AND POCKET SIDE WALLS ===")
for (label, x, y) in [("left wall", FOOT_X - POCKET_CLEAR / 2, 45.0),
                      ("right wall", FOOT_X + FOOT_W + POCKET_CLEAR / 2, 45.0),
                      ("bottom wall", 67.0, FOOT_Y - POCKET_CLEAR / 2)]:
    probe = Part.makeCylinder(0.1, 8.0, App.Vector(x, y, -0.5))
    print("  %-12s cover %.4f mm3  foot %.4f mm3" %
          (label, cover.common(probe).Volume, foot.common(probe).Volume))

print()
print("=== CLEARANCE ACROSS THE HINGE LINE (Y scan at X=67, Z=%.1f) ===" % HINGE_Z)
for y in [62.0, 63.0, 64.0, 64.6, 64.8, 65.0, 65.1, 65.2, 65.4, 66.0, 67.0, 68.0]:
    probe = Part.makeCylinder(0.3, 34.0, App.Vector(67.0, y, HINGE_Z - 17.0))
    c = cover.common(probe).Volume
    f = foot.common(probe).Volume
    print("  Y=%.1f  cover %.4f  foot %.4f  %s" %
          (y, c, f, "BOTH" if (c > 1e-4 and f > 1e-4) else ""))

print()
print("=== VOLUMES ===")
print("  cover %.1f mm3   foot %.1f mm3" % (cover.Volume, foot.Volume))
