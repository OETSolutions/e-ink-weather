"""What does the swing foot actually collide with, as a function of its open angle?

The earlier round only recorded a single total overlap per angle, which cannot say whether the
feature blocks the leg from opening FURTHER (so the case falls over when set down) or from
FOLDING (so the leg props the case up). This splits the overlap by cover feature.

Run:  freecadcmd 34_probe_stop.py
"""
import math
import os
import sys

import FreeCAD as App

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)

import importlib

m = importlib.import_module("06_rear_cover_foot")

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)


def stop_shapes():
    """The printed stop lugs and their necks, as built in build_cover."""
    out = []
    for x0 in (m.HINGE_X0 - m.PIN_ROOT, m.HINGE_X1 + 0.2):
        lug = m.Part.makeBox(6.0, m.STOP_LUG_Y, m.STOP_LUG_Z,
                             App.Vector(x0, m.STOP_LUG_Y0, m.STOP_LUG_Z0))
        lug_top = m.STOP_LUG_Z0 + m.STOP_LUG_Z
        neck_top = m.KNUCKLE_Z + m.PIN_R
        neck = m.Part.makeBox(2.0, (m.STOP_LUG_Y0 + m.STOP_LUG_Y) - (m.WEB_Y0 + 2.0),
                              neck_top - lug_top,
                              App.Vector(x0, m.WEB_Y0 + 2.0, lug_top))
        out += [lug, neck]
    return out


def main():
    src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
    outer = src.getObject("CASE_SHELL").Shape

    foot = m.build_foot()
    cover = m.build_cover(outer, foot)

    lugs = stop_shapes()
    bare = cover
    for s in lugs:
        bare = bare.cut(s)

    print("cover vol %.0f  without lugs %.0f  (lug+neck material %.0f mm3)"
          % (cover.Volume, bare.Volume, cover.Volume - bare.Volume))
    print("foot alone vol %.0f" % foot.Volume)
    print()
    print(" angle |  total   stop-lug  other   | verdict")
    prev_stop = None
    for deg in [x * 0.5 for x in range(60, 191)]:
        leg = foot.copy()
        leg.rotate(HINGE, AXIS, -deg)
        tot = leg.common(cover).Volume
        stop = leg.common(lugs[0].fuse(lugs[1]).fuse(lugs[2]).fuse(lugs[3])).Volume
        oth = leg.common(bare).Volume
        if deg % 2 == 0:
            print("  %5.1f | %7.4f  %8.4f  %7.4f" % (deg, tot, stop, oth))


main()
