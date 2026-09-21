"""Is the hinge pin actually attached to the cover, and does anything hit the anchor?

The user: "Why is the hinge pin attachment so narrow? There's extra room to make them wider."
They were right. The pin is 78.00 mm long but was anchored by two webs only 3.80 mm wide,
leaving 10.20 mm of pin cantilevered at EACH end between the web and the bearing. Sampling
the pin's own circumference showed the weld ran 81.9% only over x 29..32 and 102.4..106.

The pin cannot be anchored across its middle: x 42..92 is the clip-sweep cavity, which the
bearing has to sweep through as the leg swings. The room is at the ENDS, and there is a hard
boundary there -- the cavity wall. So this checks exactly three things, and the second is the
one that matters, because an anchor that collides with the swing is worse than a narrow one:

  1. each web reaches the cavity wall (the pin is not left cantilevered),
  2. the webs stay clear of the foot over the WHOLE swing, including past the stop,
  3. the weld really is present on the fused cover, measured on the pin's circumference.

It measures the FUSED cover, not the web boxes, because a web box can be present and still
not fuse: an earlier version set the web's Y to the cover's face and produced a bare face
contact that did not fuse in a Boolean, leaving the webs (and the pin) as separate solids.

Run:  freecadcmd 40_validate_pin_anchorage.py
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

HY, HZ, R = m.HINGE_Y, m.KNUCKLE_Z, m.PIN_R
CTR = App.Vector(0, HY, HZ)
AXIS = App.Vector(1, 0, 0)
OK = []


def check(cond, msg):
    OK.append(bool(cond))
    print("%s  %s" % ("PASS" if cond else "FAIL", msg))


def cover_shape():
    doc = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
    return doc.getObject("PRINT_REAR_COVER").Shape.copy(), \
        doc.getObject("PRINT_FOOT").Shape.copy()


def main():
    print("=== HINGE PIN ANCHORAGE ===")
    cov, foot0 = cover_shape()
    (wx0, wx1), (wy0, wy1) = m.PIN_WEB_X
    cav0, cav1 = m.BEARING_X0 - m.CAVITY_PAD, m.BEARING_X1 + m.CAVITY_PAD

    # 1. Each web reaches the cavity wall. A gap here is exactly the cantilever the user saw.
    reach_in = min(abs(wx1 - cav0), abs(wy0 - cav1))
    print("  cavity wall x %.2f / %.2f ; webs end %.2f / %.2f" % (cav0, cav1, wx1, wy0))
    check(reach_in <= 0.01,
          "each web reaches the cavity wall (gap %.2f mm; a gap is unsupported pin)" % reach_in)

    # The pin's free span: from the web's inner end to the bearing's edge.
    free = (m.BEARING_X0 - wx1, wy0 - m.BEARING_X1)
    print("  unsupported pin span per end: %.2f mm / %.2f mm" % free)
    check(max(free) <= 0.70, "pin is not left cantilevered (max free span %.2f mm)" % max(free))

    # 2. Clearance through the swing. Tested to 90 deg, past the 65 deg stop. Tested against the
    #    ANCHOR ITSELF, not the whole cover: past the stop the foot's plate reaches the cover's
    #    floor anyway (a 176 mm3 overlap at 87 deg), which is the stop's job to prevent and says
    #    nothing about the anchor. The anchor is the two web boxes, so sweep those.
    webs = []
    for x0, x1 in m.PIN_WEB_X:
        webs.append(Part.makeBox(x1 - x0, (HY + R) - m.WEB_Y0, m.WEB_Z1 - (HZ - R),
                                 App.Vector(x0, m.WEB_Y0, HZ - R)))
    worst, at = 0.0, 0
    for ang in range(0, 91):
        foot = foot0.copy()
        foot.rotate(CTR, AXIS, -float(ang))
        for w in webs:
            if w.isValid():
                v = w.common(foot).Volume
                if v > worst:
                    worst, at = v, ang
    print("  worst web/foot overlap over 0..90 deg: %.4f mm3 @ %d deg" % (worst, at))
    check(worst <= 0.001,
          "the anchor is never touched by the swing (worst %.4f mm3)" % worst)

    # 3. The weld is real, measured on the pin's own circumference.
    n = 72
    spans = []
    for i in range(48, 220):
        x = i / 2.0
        hit = 0
        for k in range(n):
            a = 2 * math.pi * k / n
            if cov.isInside(App.Vector(x, HY + (R + 0.35) * math.cos(a),
                                       HZ + (R + 0.35) * math.sin(a)), 1e-6, True):
                hit += 1
        spans.append((x, 100.0 * hit / n))
    welded = [x for x, p in spans if p >= 80.0]
    left = [x for x in welded if x < m.CASE_W / 2]
    right = [x for x in welded if x > m.CASE_W / 2]
    print("  welded bands: x %.1f..%.1f (%.1f mm) and x %.1f..%.1f (%.1f mm)"
          % (min(left), max(left), max(left) - min(left),
             min(right), max(right), max(right) - min(right)))
    check(len(left) and (max(left) - min(left)) >= 4.0,
          "left web welds >= 4.0 mm of pin (%.1f mm)" % (max(left) - min(left)))
    check(len(right) and (max(right) - min(right)) >= 4.0,
          "right web welds >= 4.0 mm of pin (%.1f mm)" % (max(right) - min(right)))
    # Both ends must weld, or the pin is anchored on one side only.
    check(len(left) >= 2 and len(right) >= 2, "both ends of the pin are welded, not one")
    # And the middle must stay OPEN -- that is the bearing's sweep cavity.
    mid = [x for x, p in spans if m.BEARING_X0 + 1.0 < x < m.BEARING_X1 - 1.0 and p > 1.0]
    check(not mid, "the bearing's sweep cavity is still open across the middle")

    print("\n%d/%d checks passed" % (sum(OK), len(OK)))


main()
