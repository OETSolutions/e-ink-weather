"""Measure the C-clip's attachment to the leg plate.

The user: "the hooks that go around the back cover rod on the swing-out foot just fall right
off after printing; their attachment to the foot is way too weak/small".

The clip ring is a CYLINDER about the hinge axis, fused to a PLATE at y 8.5..48.5. Where the
two meet they are tangent, so the weld may be a thin line rather than a volume. Measure it.
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

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
BORE_R, OUT_R, CLIP_W = m.BORE_R, m.BORE_R + m.CLIP_WALL, m.CLIP_W
CX = m.ARM_CENTERS[1]

print("plate: Y %.2f..%.2f  Z %.2f..%.2f  X %.2f..%.2f"
      % (m.FOOT_Y, m.FOOT_Y + m.FOOT_H, 0.0, m.FOOT_T, m.FOOT_X, m.FOOT_X + m.FOOT_W))
print("clip ring about axis (Y %.2f, Z %.2f): bore r %.2f, outer r %.2f, width %.2f"
      % (HY, HZ, BORE_R, OUT_R, CLIP_W))
print("ring outer top reaches Z %.2f ; plate bottom is Z %.2f" % (HZ + OUT_R, 0.0))

# The plate and the ring, built exactly as the generator does.
plate = m.rprism(m.FOOT_W, m.FOOT_H, 4.0, m.FOOT_X, m.FOOT_Y, 0.0, m.FOOT_T)
ring_full = Part.makeCylinder(OUT_R, CLIP_W,
                              App.Vector(CX - CLIP_W / 2, HY, HZ), App.Vector(1, 0, 0)).cut(
            Part.makeCylinder(BORE_R, CLIP_W + 0.2,
                              App.Vector(CX - CLIP_W / 2 - 0.1, HY, HZ), App.Vector(1, 0, 0)))

print("\n=== ring x plate overlap (the weld) ===")
ov = ring_full.common(plate)
print("   overlap volume %.4f mm3 across %d solid(s)" % (ov.Volume, len(ov.Solids)))
for s in ov.Solids:
    bb = s.BoundBox
    print("      %.4f mm3  X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\n=== how wide is the contact at the plate's face? slice the overlap along Z ===")
for z in [0.0, 0.2, 0.4, 0.6, 0.8, 1.0]:
    sec = ov.common(Part.makeBox(400, 400, 0.02, App.Vector(-200, -200, z)))
    print("   z %.1f : %.4f mm2" % (z, sec.Volume / 0.02))

print("\n=== the whole foot, as built: where does the clip join the plate? ===")
foot = m.build_foot()
print("   foot volume %.1f mm3, %d solid(s)" % (foot.Volume, len(foot.Solids)))
# Slice the foot across the clip station at several Z and report X-Y material.
for z in [-2.0, -1.5, -1.0, -0.5, 0.0, 0.5, 1.0]:
    sec = foot.common(Part.makeBox(400, 400, 0.02, App.Vector(-200, -200, z)))
    bb = sec.BoundBox
    print("   z %5.1f : area %8.3f mm2  Y %.2f..%.2f" % (z, sec.Volume / 0.02,
                                                         bb.YMin, bb.YMax))
