"""Polar map of cover material around the hinge axis, and where the leg's plate sweeps.

pa is measured as (Y-HINGE_Y)=r*sin(pa), (Z-KNUCKLE_Z)=r*cos(pa): pa=0 is +Z (into the case),
pa=90 is +Y, pa=180 is -Z (out the bottom edge).
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
foot = m.build_foot()
m.STOP_ENABLE = False
cover = m.build_cover(outer, foot)

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z


def polar_map(x, title, rmax=12.0, dr=0.5):
    print("\n%s   (X %.2f)   axis (Y %.2f Z %.2f)" % (title, x, HY, HZ))
    print("   r  " + "".join("%3d" % pa for pa in range(0, 360, 15)))
    for ri in range(1, int(rmax / dr) + 1):
        r = ri * dr
        row = ""
        for pa in range(0, 360, 15):
            a = math.radians(pa)
            y = HY + r * math.sin(a)
            z = HZ + r * math.cos(a)
            hit = False
            for f in (0.35, 0.65):
                r2 = r + f * dr
                y2 = HY + r2 * math.sin(a)
                z2 = HZ + r2 * math.cos(a)
                if cover.isInside(App.Vector(x, y2, z2), 1e-6, True):
                    hit = True
                    break
            row += "  #" if hit else "  ."
        print("%4.1f %s" % (r, row))


polar_map(56.0, "COVER, GAP station (no stop)")
polar_map(44.2, "COVER, CLIP station (no stop)")
polar_map(80.0, "COVER, near right clip")

print("\n=== plate underside reach, per angle ===")
print("corner of plate (Y 8.5, Z 0) at distance %.3f from axis" % math.hypot(3.5, 0.68))
for deg in (0, 20, 40, 55, 60, 65, 70, 80):
    th = math.radians(deg)
    cy = 5 + 3.5 * math.cos(th) + 0.68 * math.sin(th)
    cz = -0.68 - 3.5 * math.sin(th) + 0.68 * math.cos(th)
    print("  %2d deg : corner at Y %.2f Z %.2f" % (deg, cy, cz))
