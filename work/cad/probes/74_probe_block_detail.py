"""Where exactly does insertion block? Print the interference solids, split by slab."""
import os
import sys

import FreeCAD as App
import Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = doc.getObject("CASE_SHELL").Shape
foot = m.build_foot()
m.STOP_ENABLE = False
cover = m.build_cover(outer, foot)
CXS = m.ARM_CENTERS
print("clip stations %s ; gap stations 56.0 and 78.7" % CXS)


def slab(xc, w=0.4):
    return Part.makeBox(w, 300, 300, App.Vector(xc - w / 2.0, -60, -100))


CLIP_SLAB = [slab(c) for c in CXS]
GAP_SLAB = [slab(56.0), slab(78.7)]

for dz in range(-8, 9):
    g = foot.copy().translate(App.Vector(0, 0, dz))
    inter = g.common(cover)
    cb = sum(inter.common(s).Volume for s in CLIP_SLAB)
    gb = sum(inter.common(s).Volume for s in GAP_SLAB)
    print("\ndz %+3d : total %9.3f | at clips %9.3f | at gaps %9.3f"
          % (dz, inter.Volume, cb, gb))
    for s in sorted(inter.Solids, key=lambda x: -x.Volume)[:3]:
        bb = s.BoundBox
        print("        %8.3f  X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
              % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
