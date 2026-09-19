"""Where does the clip's detent rib sit in the PIN's frame as the leg opens?

The clip carries one rib (at foot-frame angle ~90, the side away from the mouth). When the leg
swings, that rib sweeps around the pin exactly like every other point of the foot. If it lands
on a matching groove at the open angle, the detent holds with NO second rib -- one bump, two
grooves -- which is both simpler to print and impossible to dislodge.

Run:  freecadcmd 37_probe_rib_sweep.py
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
AXIS = App.Vector(1, 0, 0)
CX = m.ARM_CENTERS[1]


def rib_center_folded():
    """Centre of the existing retracted rib, in the foot's own frame."""
    r = m.BORE_R - m.DETENT_H / 2.0
    return App.Vector(CX, m.HINGE_Y + r, m.KNUCKLE_Z)


def angle_about(cx, y, z):
    """Pin-frame angle of a point: 0 = +y, increasing toward -z."""
    return math.degrees(math.atan2(y - m.HINGE_Y, z - m.KNUCKLE_Z))


def main():
    p0 = rib_center_folded()
    print("rib centre folded: y %.3f z %.3f  (foot-frame angle %.1f deg)"
          % (p0.y, p0.z, angle_about(CX, p0.y, p0.z)))
    print()
    print(" open | rib centre in PIN frame | dist to pin surface")
    for k in range(0, 101, 5):
        a = math.radians(-float(k))
        dy, dz = p0.y - m.HINGE_Y, p0.z - m.KNUCKLE_Z
        py = m.HINGE_Y + dy * math.cos(a) + dz * math.sin(a)
        pz = m.KNUCKLE_Z - dy * math.sin(a) + dz * math.cos(a)
        r = math.hypot(py - m.HINGE_Y, pz - m.KNUCKLE_Z)
        aa = angle_about(CX, py, pz)
        print("  %3d  |  angle %7.2f deg  r %.3f  |  crest at %.3f vs pin %.3f" %
              (k, aa, r, r + m.DETENT_H / 2.0, m.PIN_R))


main()
