"""Fillet the clip gusset's exposed corners and measure the weld.

The gusset is a block in the wedge between the ring's outer wall and the plate's underside. Its
exposed (non-buried) corners are its bottom two long edges. Fillet those (convex, builds
reliably), then fuse, and check the weld survives.
"""
import os, sys, math
import FreeCAD as App, Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib
m = importlib.import_module("06_rear_cover_foot")

CX = m.ARM_CENTERS[1]
x0 = CX - m.CLIP_W / 2
Y0, Y1 = m.CLIP_GUSSET_Y0, m.CLIP_GUSSET_Y1
ZB, ZT = m.CLIP_GUSSET_Z0, m.CLIP_GUSSET_Z1
plate = m.rprism(m.FOOT_W, m.FOOT_H, 4.0, m.FOOT_X, m.FOOT_Y, 0.0, m.FOOT_T)
ring = Part.makeCylinder(m.BORE_R + m.CLIP_WALL, m.CLIP_W,
                         App.Vector(x0, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0)).cut(
       Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.2,
                         App.Vector(x0 - 0.1, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0)))

def bore(s):
    return s.cut(Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.4,
                                   App.Vector(x0 - 0.2, m.HINGE_Y, m.KNUCKLE_Z),
                                   App.Vector(1, 0, 0)))

def weld(g):
    return g.common(ring).Volume + g.common(plate).Volume

box = Part.makeBox(m.CLIP_W, Y1 - Y0, ZT - ZB, App.Vector(x0, Y0, ZB))
print("   %-30s weld %7.3f  valid %s" % ("plain box (current)", weld(bore(box)), bore(box).isValid()))

# fillet the box's two bottom long edges: along X at (Y0,ZB) and (Y1,ZB)
def bottom_edges(s):
    out = []
    for e in s.Edges:
        bb = e.BoundBox
        if bb.XMax - bb.XMin < m.CLIP_W - 0.01:      # need the full-width edges
            continue
        y = round((bb.YMin + bb.YMax) / 2, 3); z = round((bb.ZMin + bb.ZMax) / 2, 3)
        if abs(z - ZB) < 0.02 and (abs(y - Y0) < 0.02 or abs(y - Y1) < 0.02):
            out.append(e)
    return out

for R in (0.6, 0.8, 1.0, 1.2, 1.5, 2.0, 2.5):
    es = bottom_edges(box)
    try:
        g = box.makeFillet(R, es)
        gb = bore(g)
        print("   %-30s weld %7.3f  valid %-5s solids %d"
              % ("bottom fillet R%.1f (%d edges)" % (R, len(es)), weld(gb), gb.isValid(),
                 len(gb.Solids)))
    except Exception as e:
        print("   bottom fillet R%.1f : FAILED %s" % (R, str(e)[:48]))

# also try filleting just the -Y bottom edge (the one facing the hinge, most exposed)
for R in (1.0, 1.5, 2.0):
    es = [e for e in bottom_edges(box)
          if abs((e.BoundBox.YMin + e.BoundBox.YMax) / 2 - Y0) < 0.02]
    try:
        g = bore(box.makeFillet(R, es))
        print("   %-30s weld %7.3f  valid %-5s solids %d"
              % ("-Y bottom fillet R%.1f" % R, weld(g), g.isValid(), len(g.Solids)))
    except Exception as e:
        print("   -Y bottom fillet R%.1f : FAILED %s" % (R, str(e)[:48]))
