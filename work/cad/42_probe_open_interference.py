"""What does the OPEN foot actually collide with on the cover?

The user reports the motion reference model shows interference with "a blocky piece sticking
out from the hinge pin". That would stop the leg reaching its stop, which is exactly how a
stand ends up with nothing holding it open. Classify every interfering solid by location.

Run:  freecadcmd 42_probe_open_interference.py
"""
import math
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)


def classify(sol):
    bb = sol.BoundBox
    tags = []
    if bb.YMin >= m.STOP_LUG_Y0 - 0.6 and bb.YMax <= m.STOP_LUG_Y0 + m.STOP_LUG_Y + 0.6:
        tags.append("STOP-LUG band")
    if bb.ZMax <= m.KNUCKLE_Z + m.PIN_R + 0.2 and bb.YMin < m.HINGE_Y + 0.5:
        tags.append("under/at pin")
    for i, cx in enumerate(m.ARM_CENTERS):
        if bb.XMin >= cx - m.CLIP_W - 0.3 and bb.XMax <= cx + m.CLIP_W + 0.3:
            tags.append("clip band %d" % i)
            break
    return ",".join(tags) or "OTHER"


def main():
    src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
    outer = src.getObject("CASE_SHELL").Shape
    foot = m.build_foot()
    cover = m.build_cover(outer, foot)

    # The stop lugs + necks, so they can be named separately.
    stops = []
    for x0 in (m.HINGE_X0 - m.PIN_ROOT, m.HINGE_X1 + 0.2):
        lug = Part.makeBox(6.0, m.STOP_LUG_Y, m.STOP_LUG_Z,
                           App.Vector(x0, m.STOP_LUG_Y0, m.STOP_LUG_Z0))
        stops.append(lug)
    stop_solid = stops[0].fuse(stops[1])

    print("lug X ranges: %.2f..%.2f and %.2f..%.2f   (plate X %.2f..%.2f)"
          % (m.HINGE_X0 - m.PIN_ROOT, m.HINGE_X0 - m.PIN_ROOT + 6.0,
             m.HINGE_X1 + 0.2, m.HINGE_X1 + 6.2, m.FOOT_X, m.FOOT_X + m.FOOT_W))
    print("lug Y %.2f..%.2f  Z %.2f..%.2f" % (m.STOP_LUG_Y0, m.STOP_LUG_Y0 + m.STOP_LUG_Y,
                                              m.STOP_LUG_Z0, m.STOP_LUG_Z0 + m.STOP_LUG_Z))
    print()

    for deg in (0, 30, 50, 56, 60, 65, 70, 75, 85):
        leg = foot.copy()
        leg.rotate(HINGE, App.Vector(1, 0, 0), -float(deg))
        tot = leg.common(cover).Volume
        st = leg.common(stop_solid).Volume
        print("open %2d deg: total %.4f mm3 | stop-lug share %.4f | rest %.4f"
              % (deg, tot, st, tot - st))
        if deg in (65, 75) and tot - st > 0.01:
            rest = leg.common(cover.cut(stop_solid))
            for s in rest.Solids:
                bb = s.BoundBox
                print("      %-24s vol %.4f  X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
                      % (classify(s), s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax,
                         bb.ZMin, bb.ZMax))


main()
