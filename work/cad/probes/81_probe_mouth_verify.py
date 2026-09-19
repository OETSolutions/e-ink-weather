"""Verify the mouth-orientation fix on the REAL generator.

Sweeps the insertion translation for both mouth orientations and reports off-bearing
interference at each step. The mouth must face the direction the foot approaches from.
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

doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = doc.getObject("CASE_SHELL").Shape
HY, HZ = m.HINGE_Y, m.KNUCKLE_Z


def offbearing(shape, cover):
    inter = shape.common(cover)
    tot = inter.Volume
    for c in m.ARM_CENTERS:
        s = Part.makeBox(3.4, 300, 300, App.Vector(c - 1.7, -60, -100))
        tot -= inter.common(s).Volume
    return max(tot, 0.0), inter.Volume


for ang in (0.0, 180.0):
    m.MOUTH_PIN_ANGLE = ang
    m.DETENT_OPEN_PIN_ANGLE = ang + 180.0 + m.DETENT_OPEN_ANGLE
    foot = m.build_foot()
    m.STOP_ENABLE = False
    cover = m.build_cover(outer, foot)
    print("\n" + "=" * 76)
    print("MOUTH_PIN_ANGLE = %.0f  (foot solids %d valid %s vol %.1f)"
          % (ang, len(foot.Solids), foot.isValid(), foot.Volume))
    print("=" * 76)
    print("  dz    off-bearing   total-interf   verdict")
    for dz in list(range(9, -1, -1)) + list(range(-1, -11, -1)):
        f = foot.copy()
        f.translate(App.Vector(0, 0, dz))
        ob, tot = offbearing(f, cover)
        print("  %+3d   %11.3f   %12.3f   %s"
              % (dz, ob, tot, "BLOCKED" if ob > 0.05 else "clear"))
