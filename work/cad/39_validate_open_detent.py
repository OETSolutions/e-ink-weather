"""Does the leg now hold itself open? Measure the detent's holding torque vs open angle.

The detent is only real if the leg has to be pushed over a crest to fold: the interference
between the clip's rib and the pin must rise, peak, and fall as the leg sweeps past the open
groove, and the holding moment must exceed what gravity asks for at that angle.

Gravity's demand comes from 36_kickstand_energy.py: about 2.8 N.mm at 65 deg, rising to
~14 N.mm if the leg is folded most of the way.

Run:  freecadcmd 39_validate_open_detent.py
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

OK = []


def check(cond, msg):
    OK.append(bool(cond))
    print("%s  %s" % ("PASS" if cond else "FAIL", msg))


def main():
    doc = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
    foot = doc.getObject("PRINT_FOOT").Shape
    cover = doc.getObject("PRINT_REAR_COVER").Shape
    print("foot vols %d  cover vols %d" % (len(foot.Solids), len(cover.Solids)))

    # Interference between the clip ribs and the pin, per angle. It is NOT enough to test
    # against an ideal cylinder: a plain pin is axisymmetric, so every angle gives the same
    # answer and the detent would look invisible. The pin that matters is the one the cover
    # actually prints -- with the two scallops cut into it -- so take the cover's material
    # inside the pin radius, over the clips' axial band.
    pin_band = m.cyl_x(m.HINGE_X0 - m.PIN_ROOT, (m.HINGE_X1 - m.HINGE_X0) + 2 * m.PIN_ROOT,
                       m.HINGE_Y, m.KNUCKLE_Z, m.PIN_R)
    pin = cover.common(pin_band)
    print("cover's actual pin material in the clip band: %.3f mm3" % pin.Volume)

    print()
    print(" open | rib-vs-pin overlap mm3 | relative")
    vals = []
    for k in range(50, 81):
        leg = foot.copy()
        leg.rotate(HINGE, AXIS, -float(k))
        v = leg.common(pin).Volume
        vals.append((k, v))
    peak = max(v for _, v in vals)
    base = min(v for _, v in vals)
    for k, v in vals:
        bar = "#" * int(40 * (v - base) / max(peak - base, 1e-9))
        print("  %3d  | %10.4f              | %s" % (k, v, bar))

    at65 = dict(vals)[65]
    check(peak > base + 0.05,
          "a crest exists: overlap varies %.4f..%.4f mm3 across the swing" % (base, peak))
    # The crest must sit AT the open angle: 65 deg has to be a strict local MINIMUM of
    # interference (the rib seated in its groove) with higher values either side, so the leg
    # has to be pushed over a crest in either direction.
    v60, v61, v65, v69, v70 = (dict(vals)[k] for k in (60, 61, 65, 69, 70))
    check(v65 < v61 and v65 < v69,
          "65 deg is a local minimum of interference (a seated detent)")
    check(v65 <= min(vals, key=lambda t: t[1])[1] + 1e-9,
          "65 deg is the DEEPEST seat in the whole swing")
    # Folding must require climbing: interference at 60 and 70 must exceed that at 65.
    check(v60 > v65 and v70 > v65,
          "folding and over-opening both climb a crest (60:%.4f 65:%.4f 70:%.4f)"
          % (v60, v65, v70))

    # Holding force: rib height engaged x radial stiffness per unit length.
    # Reported for judgement, not asserted (stiffness estimate is approximate).
    print()
    print("seated interference at 65 deg: %.4f mm3 over 3 clips" % at65)
    print("crest height %.2f mm, rib width %.2f mm, pin r %.2f, preload %.2f mm"
          % (m.DETENT_H, m.DETENT_W, m.PIN_R, m.PIN_PRELOAD))

    # Confirm the mouth opening is still the designed 150 deg.
    ring = Part.makeCylinder(m.BORE_R + m.CLIP_WALL, m.CLIP_W,
                             App.Vector(m.ARM_CENTERS[1] - m.CLIP_W / 2,
                                        m.HINGE_Y, m.KNUCKLE_Z),
                             App.Vector(1, 0, 0))
    solid = Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.2,
                              App.Vector(m.ARM_CENTERS[1] - m.CLIP_W / 2 - 0.1,
                                         m.HINGE_Y, m.KNUCKLE_Z),
                              App.Vector(1, 0, 0))
    clip = ring.cut(solid)
    foot_clip = foot.common(clip)
    open_arc = []
    for k in range(0, 360):
        a = math.radians(k)
        r = m.BORE_R + 0.3
        p = App.Vector(m.ARM_CENTERS[1], m.HINGE_Y + r * math.sin(a),
                       m.KNUCKLE_Z + r * math.cos(a))
        if not foot_clip.isInside(p, 1e-6, True):
            open_arc.append(k)
    frac = len(open_arc) / 360.0
    print()
    print("mouth opening measured %.1f deg (design %.0f)" % (frac * 360.0, m.MOUTH_OPEN_DEG if hasattr(m, "MOUTH_OPEN_DEG") else 150.0))

    print()
    print("%d/%d checks pass" % (sum(OK), len(OK)))


main()
