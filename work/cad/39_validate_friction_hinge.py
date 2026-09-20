"""Does the plain friction hinge still seat, retain and open?

This file used to validate the OPEN-POSITION DETENT -- a rib in the clip climbing over a
crest in the pin at 65 deg. That mechanism is GONE (user: "Get RID of the nubs!! They
still cause the end of the hooks to flare out and hit!!!"), so there is no crest to
validate. What replaced it is a plain interference (friction) hinge: the clip bore is
0.12 mm smaller than the pinned radius, so it grips the pin and stays wherever it is put.

So this now checks the three things a friction hinge actually has to do:
  1. the clip grips -- the bore interference is present and is real material contact,
  2. interference is NEARLY CONSTANT through the swing (no crest, no flare at the lips),
  3. the mouth still opens enough for the pin to be pressed in and the clip to retain it.

Run:  freecadcmd 39_validate_friction_hinge.py
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

    # The pin the cover actually prints: the cover's material inside the pin radius, over the
    # clips' axial band. The pin is now a clean cylinder -- there are no detent grooves cut in
    # it -- so this should equal the ideal cylinder volume to within tessellation error. If a
    # groove were ever re-added it would show up here as missing volume.
    pin_band = m.cyl_x(m.HINGE_X0 - m.PIN_ROOT, (m.HINGE_X1 - m.HINGE_X0) + 2 * m.PIN_ROOT,
                       m.HINGE_Y, m.KNUCKLE_Z, m.PIN_R)
    pin = cover.common(pin_band)
    ideal = math.pi * m.PIN_R**2 * ((m.HINGE_X1 - m.HINGE_X0) + 2 * m.PIN_ROOT)
    print("cover's pin material in the clip band: %.3f mm3" % pin.Volume)
    print("ideal cylinder over the same span:     %.3f mm3" % ideal)
    check(pin.Volume > ideal * 0.97,
          "pin is a plain cylinder, no detent grooves cut into it "
          "(%.2f of %.2f mm3)" % (pin.Volume, ideal))

    # Interference through the swing. Against a plain axisymmetric pin the overlap between the
    # clip and the pin is the WHOLE interference band at every angle, so it must be essentially
    # FLAT: any rise/fall here would be a crest, i.e. a detent, which is what was removed. This
    # is also the check that would have caught the flare: a nub in the bore made the seated
    # interference vary sharply with angle.
    print()
    print(" open | clip-vs-pin overlap mm3")
    vals = []
    for k in range(50, 81):
        leg = foot.copy()
        leg.rotate(HINGE, AXIS, -float(k))
        vals.append((k, leg.common(pin).Volume))
    vmin = min(v for _, v in vals)
    vmax = max(v for _, v in vals)
    for k, v in vals:
        print("  %3d  | %10.4f" % (k, v))
    check(vmax - vmin < 0.02 * vmax,
          "no crest through the swing -- interference is flat (%.4f..%.4f mm3, spread %.2f%%)"
          % (vmin, vmax, 100.0 * (vmax - vmin) / max(vmax, 1e-9)))
    check(vmin > 0.5, "the clip grips the pin at every angle (min %.4f mm3)" % vmin)

    # Grip pressure. The friction holding torque is mu * N * r, and N is set by the bore
    # interference over the clip's contact area. Reported for judgement against gravity's
    # 2.8 N.mm demand at 65 deg (see 36_kickstand_energy.py), not asserted -- the radial
    # stiffness of a printed ring is only an estimate here.
    print()
    print("seated interference at 65 deg: %.4f mm3 over 3 clips" % dict(vals)[65])
    print("bore interference %.2f mm, clip wall %.2f mm, pin r %.2f mm"
          % (m.PIN_PRELOAD, m.CLIP_WALL, m.PIN_R))

    # Mouth opening: the pin must be pressed in and then retained past the equator.
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
    print("mouth opening measured %.1f deg (design %.0f)"
          % (frac * 360.0, getattr(m, "MOUTH_OPEN_DEG", 150.0)))
    check(frac * 360.0 >= 90.0, "mouth is open enough to press the pin in")
    check(frac * 360.0 <= 175.0, "wrap is enough to retain the pin past its equator")

    print()
    print("%d/%d checks pass" % (sum(OK), len(OK)))


main()
