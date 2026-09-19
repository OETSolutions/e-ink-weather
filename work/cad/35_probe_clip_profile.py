"""Map the foot clip's actual shape in its own frame, and the cover stop lug's face.

Everything the detent depends on: where the clip's wall runs, where the mouth is, and how far
out from the hinge axis the lug sits at each angle.

Run:  freecadcmd 35_probe_clip_profile.py
"""
import math
import os
import sys

import FreeCAD as App

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

HINGE_Z = m.KNUCKLE_Z


def profile(shape, cx, tag):
    """For cover-frame angle psi (from +y toward -z) and a fine radius sweep, report the
    first and last radius that lies inside material, in a thin slab about x=cx."""
    slab = shape.common(App.makeBox_host if False else __import__("Part").makeBox(
        0.4, 40.0, 40.0, App.Vector(cx - 0.2, m.HINGE_Y - 20.0, HINGE_Z - 20.0)))
    print("== %s  slab vol %.2f" % (tag, slab.Volume))
    out = []
    for k in range(0, 360, 5):
        a = math.radians(k)
        rs = [0.4 + 0.04 * i for i in range(100)]
        hits = [r for r in rs
                if slab.isInside(App.Vector(cx, m.HINGE_Y + r * math.sin(a),
                                            HINGE_Z + r * math.cos(a)), 1e-6, True)]
        if not hits:
            out.append((k, None, None))
        else:
            out.append((k, min(hits), max(hits)))
    for k, lo, hi in out:
        if lo is None:
            print("   psi %3d  OPEN" % k)
        else:
            print("   psi %3d  r %.2f .. %.2f" % (k, lo, hi))


def main():
    foot = m.build_foot()
    profile(foot, m.ARM_CENTERS[1], "FOOT clip at cx=%.1f" % m.ARM_CENTERS[1])
    src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
    outer = src.getObject("CASE_SHELL").Shape
    cover = m.build_cover(outer, foot)
    profile(cover, m.ARM_CENTERS[1], "COVER at cx=%.1f" % m.ARM_CENTERS[1])


main()
