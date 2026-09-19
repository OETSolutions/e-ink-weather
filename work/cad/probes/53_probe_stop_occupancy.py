"""Find the stop location directly, from occupancy.

For a grid of (pin angle, radius) cells, mark whether the rotating leg occupies that cell at
each open angle. A valid stop sits in a cell that is EMPTY for every angle up to ~62 deg and
OCCUPIED at 64-66 deg: that is the only place a stop can both clear the swing and arrest it
past the gravity barrier.

Sampled at two X stations: a clip station and mid-plate.
"""
import os
import sys
import math

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
foot = m.build_foot()

ANGLES = [float(a) for a in range(40, 72)]
RADII = [r / 2.0 for r in range(4, 34)]          # 2.0 .. 16.5
PAS = range(0, 360, 2)


def occupancy(x):
    """occ[pa][r] = set of open angles at which (pa, r) is inside the leg at station x."""
    occ = {}
    for pa in PAS:
        a = math.radians(pa)
        for r in RADII:
            y = HY + r * math.sin(a)
            z = HZ + r * math.cos(a)
            hits = []
            for d in ANGLES:
                leg = foot.copy()
                leg.rotate(HINGE, AXIS, -d)
                if leg.isInside(App.Vector(x, y, z), 0.0, True):
                    hits.append(d)
            if hits:
                occ[(pa, r)] = hits
    return occ


def report(name, x):
    occ = occupancy(x)
    good = []
    for (pa, r), hits in occ.items():
        lo, hi = min(hits), max(hits)
        if lo >= 63.0 and hi <= 70.0:
            good.append((lo, pa, r, lo, hi))
    good.sort()
    print("\n=== %s (x=%.1f): cells free until >=63 deg, occupied by <=70 deg ===" % (name, x))
    if not good:
        print("   none")
        return
    for lo, pa, r, a, b in good[:30]:
        print("   pin angle %3d  r %4.1f  ->  occupied at %s"
              % (pa, r, ",".join("%.0f" % v for v in sorted(occ[(pa, r)]))))
    # Convert the earliest to Y,Z for placing a box.
    lo, pa, r, a, b = good[0]
    aa = math.radians(pa)
    print("   FIRST such cell: pin angle %d r %.1f -> Y %.2f Z %.2f"
          % (pa, r, HY + r * math.sin(aa), HZ + r * math.cos(aa)))


report("clip station", m.ARM_CENTERS[0])
report("mid plate", (m.FOOT_X + m.FOOT_W / 2))
