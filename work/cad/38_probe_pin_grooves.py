"""Does the cover's pin actually carry the detent grooves, and at which pin angles?

Earlier work assumed a groove at pin angle 90 and added a second one at 205. Both assumptions
need checking against the saved solid before any more geometry is written.

Run:  freecadcmd 38_probe_pin_grooves.py
"""
import math
import os
import sys

import FreeCAD as App

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

CX = m.ARM_CENTERS[1]


def main():
    src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
    outer = src.getObject("CASE_SHELL").Shape
    cover = m.build_cover(outer, None)

    # Thin annulus at the pin surface, limited to the clip's axial width.
    ring = m.cyl_x(CX - m.CLIP_W / 2, m.CLIP_W, m.HINGE_Y, m.KNUCKLE_Z, m.PIN_R - 0.02)
    ring = ring.cut(m.cyl_x(CX - m.CLIP_W / 2 - 0.1, m.CLIP_W + 0.2,
                            m.HINGE_Y, m.KNUCKLE_Z, m.PIN_R - 0.42))
    shell = cover.common(ring)
    print("pin outer shell (r %.2f..%.2f) volume %.3f mm3"
          % (m.PIN_R - 0.42, m.PIN_R - 0.02, shell.Volume))

    print()
    print(" pin angle | material in the shell (a groove shows as a gap)")
    for k in range(0, 360, 3):
        a = math.radians(k)
        r = m.PIN_R - 0.22
        p = App.Vector(CX, m.HINGE_Y + r * math.sin(a), m.KNUCKLE_Z + r * math.cos(a))
        box = App.makeBox if False else __import__("Part").makeBox(
            m.CLIP_W, 0.10, 0.10, App.Vector(CX - m.CLIP_W / 2, p.y - 0.05, p.z - 0.05))
        v = shell.common(box).Volume
        print("   %3d      | %s (%.4f)" % (k, "#" if v > 1e-6 else ".", v))

    # Also confirm which pin angles the existing RIB would reach, from the real foot.
    foot = m.build_foot()
    hinge = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
    print()
    print("rib pin-angle at various open angles (from the real foot solid):")
    for deg in (0, 20, 40, 55, 60, 65, 70, 80, 90):
        leg = foot.copy()
        leg.rotate(hinge, App.Vector(1, 0, 0), -float(deg))
        best = None
        for s in leg.Solids:
            if s.Volume < 100:
                continue
            for v in s.Vertexes:
                if abs(v.Point.x - CX) > m.CLIP_W:
                    continue
                r = math.hypot(v.Point.y - m.HINGE_Y, v.Point.z - m.KNUCKLE_Z)
                if best is None or r > best[0]:
                    a = math.degrees(math.atan2(v.Point.y - m.HINGE_Y,
                                                v.Point.z - m.KNUCKLE_Z))
                    best = (r, a)
        print("   open %2d deg -> outermost clip vertex r %.3f at pin angle %.1f"
              % (deg, best[0], best[1]))


main()
