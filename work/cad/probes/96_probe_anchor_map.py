"""Map the cover's material around the hinge, then test candidate attachments.

Two questions:
  1. What cover material exists near the hinge axis at (a) an END-WEB X, where the stop must
     anchor, and (b) a CLIP-STATION X, where the clip must attach to the plate?
  2. How much weld does a deeper riser buy, and how much does a clip root gusset buy?
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

src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = src.getObject("CASE_SHELL").Shape
m.STOP_ENABLE = False
cover = m.build_cover(outer, m.build_foot())
m.STOP_ENABLE = True

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
R_CAV = m.BORE_R + m.CLIP_WALL + 0.6

print("=== cover Y-Z occupancy (r = distance from hinge axis %.2f,%.2f ; cavity r %.2f) ==="
      % (HY, HZ, R_CAV))
for X in (30.5, 44.2, 50.85):
    print("\n  --- X %.2f ---" % X)
    print("        " + "".join("%7.1f" % z for z in range(-6, 4, 1)))
    for y in range(0, 13, 1):
        row = "  y %4.1f" % y
        for z in range(-6, 4, 1):
            p = App.Vector(X, y + 0.0, z + 0.0)
            inside = cover.isInside(p, 1e-6, True)
            d = math.hypot(y - HY, z - HZ)
            row += "  %5s" % ("CAV" if d < R_CAV and inside else ("#" if inside else "."))
        print(row)
    print("        (CAV = cover solid but inside the clip-sweep cylinder)")

print("\n=== riser depth: weld vs STOP_RISER_Z1 (riser X 29.40..31.80, Y 4.00..7.20) ===")
for z1 in (-2.0, -1.0, 0.0, 1.0, 2.0, 2.8):
    riser = Part.makeBox(31.80 - 29.40, 7.20 - 4.00, z1 - (-5.00),
                         App.Vector(29.40, 4.00, -5.00))
    w = riser.common(cover)
    bb = w.BoundBox
    print("   Z1 %5.1f : weld %8.3f mm3   Z %.2f..%.2f  (engagement %.2f mm)"
          % (z1, w.Volume, bb.ZMin, bb.ZMax, bb.ZMax - bb.ZMin))

print("\n=== clip root gusset candidates (per clip, so x3 total) ===")
plate = m.rprism(m.FOOT_W, m.FOOT_H, 4.0, m.FOOT_X, m.FOOT_Y, 0.0, m.FOOT_T)
CX = m.ARM_CENTERS[1]
bore = Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.2,
                         App.Vector(CX - m.CLIP_W / 2 - 0.1, HY, HZ), App.Vector(1, 0, 0))
ring = Part.makeCylinder(m.BORE_R + m.CLIP_WALL, m.CLIP_W,
                         App.Vector(CX - m.CLIP_W / 2, HY, HZ), App.Vector(1, 0, 0)).cut(bore)
for y0, y1, z0, z1 in [(7.75, 10.50, -2.00, 1.80),
                       (7.75, 10.50, -2.80, 1.80),
                       (7.75, 12.00, -2.80, 1.80),
                       (7.60, 11.00, -3.60, 1.80),
                       (7.75, 10.50, -2.80, 3.00)]:
    g = Part.makeBox(m.CLIP_W, y1 - y0, z1 - z0,
                     App.Vector(CX - m.CLIP_W / 2, y0, z0))
    w_ring = g.common(ring)
    w_plate = g.common(plate)
    gfoot = g.cut(ring.cut(bore))
    w_bplate = gfoot.common(plate)
    print("   Y %.2f..%.2f Z %.2f..%.2f : ring weld %7.3f  plate weld %8.3f  total %8.3f mm3"
          % (y0, y1, z0, z1, w_ring.Volume, w_plate.Volume,
             w_ring.Volume + w_plate.Volume))

print("\n=== does a gusset foul the mouth channel? (mouth box is Y 2.296..7.704) ===")
print("   all candidates start at Y >= 7.60 ; mouth box +Y edge = 7.704")
print("   NOTE: Y0 7.60 candidate OVERLAPS the mouth box -> check below")
for y0 in (7.60, 7.75):
    g = Part.makeBox(m.CLIP_W, 10.50 - y0, 1.80 - (-2.80),
                     App.Vector(CX - m.CLIP_W / 2, y0, -2.80))
    mouth = Part.makeBox(m.CLIP_W + 0.4, m.BORE_R * 2.6, m.BORE_R + m.CLIP_WALL - 0.5388,
                         App.Vector(CX - m.CLIP_W / 2 - 0.2, HY - m.BORE_R * 1.3,
                                    HZ - (m.BORE_R + m.CLIP_WALL)))
    mouth.rotate(App.Vector(CX, HY, HZ), App.Vector(1, 0, 0), m.MOUTH_PIN_ANGLE - 180.0)
    ov = g.common(mouth)
    print("   Y0 %.2f : gusset x mouth-box volume %.4f mm3" % (y0, ov.Volume))
