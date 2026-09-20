"""Do the new clip gussets add material that sweeps into the cover during assembly?

Validator 29 clears the foot for rotational insertion, but it excludes any overlap within
1.7 mm of a clip station's X -- and the gussets sit exactly there. So it cannot see a gusset
collision by construction. This probe looks straight at the gussets.

Method: build the gusset solids alone, rotate them with the foot, and measure their overlap
with the cover at every angle of the assembly range. They must never touch it.
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

d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
cover = d.getObject("PRINT_REAR_COVER").Shape

# The gussets, exactly as build_foot makes them.
gussets = None
for cx in m.ARM_CENTERS:
    g = Part.makeBox(m.CLIP_W, m.CLIP_GUSSET_Y1 - m.CLIP_GUSSET_Y0,
                     m.CLIP_GUSSET_Z1 - m.CLIP_GUSSET_Z0,
                     App.Vector(cx - m.CLIP_W / 2, m.CLIP_GUSSET_Y0, m.CLIP_GUSSET_Z0))
    g = g.cut(Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.4,
                                App.Vector(cx - m.CLIP_W / 2 - 0.2, m.HINGE_Y, m.KNUCKLE_Z),
                                App.Vector(1, 0, 0)))
    gussets = g if gussets is None else gussets.fuse(g)
print("gusset solids: %.1f mm3, %d solid(s)" % (gussets.Volume, len(gussets.Solids)))

# The ring, for comparison -- pre-existing material that was already proven clear.
ring = None
for cx in m.ARM_CENTERS:
    r = Part.makeCylinder(m.BORE_R + m.CLIP_WALL, m.CLIP_W,
                          App.Vector(cx - m.CLIP_W / 2, m.HINGE_Y, m.KNUCKLE_Z),
                          App.Vector(1, 0, 0)).cut(
        Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.2,
                          App.Vector(cx - m.CLIP_W / 2 - 0.1, m.HINGE_Y, m.KNUCKLE_Z),
                          App.Vector(1, 0, 0)))
    ring = r if ring is None else ring.fuse(r)

A = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
print("\n=== gusset vs cover, rotated through the whole assembly range ===")
worst = 0.0
for deg in range(0, 121, 5):
    gg = gussets.copy()
    if deg:
        gg.rotate(A, App.Vector(1, 0, 0), -float(deg))
    ov = gg.common(cover).Volume
    worst = max(worst, ov)
    print("   %3d deg : gusset/cover overlap %8.4f mm3  %s"
          % (deg, ov, "OK" if ov < 1e-6 else "<-- COLLIDES"))
print("   worst %.4f mm3" % worst)

print("\n=== is the gusset inside the clip-sweep cavity (r %.2f)? ==="
      % (m.BORE_R + m.CLIP_WALL + 0.6))
R = m.BORE_R + m.CLIP_WALL + 0.6
outside = 0.0
for cx in m.ARM_CENTERS:
    for y in [m.CLIP_GUSSET_Y0 + 0.1 * i for i in range(45)]:
        for z in [m.CLIP_GUSSET_Z0 + 0.1 * i for i in range(47)]:
            p = App.Vector(cx, y, z)
            if math.hypot(y - m.HINGE_Y, z - m.KNUCKLE_Z) > R:
                if gussets.isInside(p, 1e-6, True) and not cover.isInside(p, 1e-6, True):
                    outside += 1
print("   sampled gusset points outside the cavity AND outside the cover: %d" % outside)
print("   (0 is not required: material outboard of the cavity is the plate itself)")

print("\n=== but the real test: does the gusset overlap the PLATE (same body, fine) or the")
print("    cover's LOWER WALL (a collision)?  Sweep the leg and look at the wall band only ===")
# The cover's lower wall region: y 0..8, the chamfered shell edge, at all z.
wall = cover.common(Part.makeBox(400, 9.0, 60.0, App.Vector(-200, -1.0, -30.0)))
print("   lower-wall reference volume: %.1f mm3" % wall.Volume)
for deg in [90, 70, 65, 60, 50, 40, 20, 0]:
    gg = gussets.copy()
    if deg:
        gg.rotate(A, App.Vector(1, 0, 0), -float(deg))
    ov = gg.common(wall).Volume
    print("   %3d deg : gusset vs lower wall %8.4f mm3  %s"
          % (deg, ov, "OK" if ov < 1e-6 else "<-- HITS THE WALL"))
