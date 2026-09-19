"""At what angle does the leg first hit the stop, and with WHICH features?

A rigid lug against a rigid plate is a HARD stop: the leg physically cannot rotate past the
first contact angle. If that angle is on the folding side of the gravity barrier, the stand
collapses no matter how strong the detent is. So the number that matters is the FIRST-contact
angle, and where the contact is.

Run:  freecadcmd 43_probe_stop_contact.py
"""
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


def stop_boxes(lug_y0, lug_z0=None):
    """The stop lugs and the necks that anchor them, as build_cover makes them."""
    z0 = m.STOP_LUG_Z0 if lug_z0 is None else lug_z0
    lugs, necks = [], []
    for x0 in (m.HINGE_X0 - m.PIN_ROOT, m.HINGE_X1 + 0.2):
        lugs.append(Part.makeBox(6.0, m.STOP_LUG_Y, m.STOP_LUG_Z,
                                 App.Vector(x0, lug_y0, z0)))
        lug_top = z0 + m.STOP_LUG_Z
        necks.append(Part.makeBox(2.0, (lug_y0 + m.STOP_LUG_Y) - (m.WEB_Y0 + 2.0),
                                  (m.KNUCKLE_Z + m.PIN_R) - lug_top,
                                  App.Vector(x0, m.WEB_Y0 + 2.0, lug_top)))
    return lugs, necks


def main():
    foot = m.build_foot()

    print("current stop: lug Y %.2f..%.2f  Z %.2f..%.2f"
          % (m.STOP_LUG_Y0, m.STOP_LUG_Y0 + m.STOP_LUG_Y, m.STOP_LUG_Z0,
             m.STOP_LUG_Z0 + m.STOP_LUG_Z))
    lugs, necks = stop_boxes(m.STOP_LUG_Y0)
    lugS = lugs[0].fuse(lugs[1])
    neckS = necks[0].fuse(necks[1])
    print()
    print(" deg | lug contact | neck contact | first-contact bbox")
    for deg in range(50, 78):
        leg = foot.copy()
        leg.rotate(HINGE, AXIS, -float(deg))
        lv = leg.common(lugS)
        nv = leg.common(neckS)
        note = ""
        if lv.Volume > 0.005:
            bb = lv.BoundBox
            note = "LUG  X %.1f..%.1f Y %.2f..%.2f Z %.2f..%.2f" % (
                bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax)
        elif nv.Volume > 0.005:
            bb = nv.BoundBox
            note = "NECK X %.1f..%.1f Y %.2f..%.2f Z %.2f..%.2f" % (
                bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax)
        print(" %3d | %11.4f | %12.4f | %s" % (deg, lv.Volume, nv.Volume, note))

    # Which foot feature is at the contact? Look for foot vertices inside the lug box.
    print()
    leg0 = foot.copy()
    leg0.rotate(HINGE, AXIS, -56.0)
    bb = lugS.BoundBox
    near = []
    for s in leg0.Solids:
        for v in s.Vertexes:
            p = v.Point
            if (bb.XMin - 0.5 <= p.x <= bb.XMax + 0.5 and
                    bb.YMin - 1.0 <= p.y <= bb.YMax + 1.0 and
                    bb.ZMin - 1.0 <= p.z <= bb.ZMax + 1.0):
                near.append((round(p.x, 2), round(p.y, 2), round(p.z, 2)))
    print("foot vertices within 1 mm of the lug at 56 deg: %d" % len(near))
    for p in sorted(set(near))[:12]:
        print("    x %.2f y %.2f z %.2f" % p)


main()
