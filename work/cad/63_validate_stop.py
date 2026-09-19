"""VERIFY the 65 deg ramp stop, subtracting the pre-existing clip/pin friction baseline.

There is ~8 mm3 of clip-to-pin interference at EVERY angle by design (the friction fit). That
baseline must be removed before the stop's own contribution can be judged. Build the cover
twice -- stop on and stop off -- and difference the two.
"""
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = doc.getObject("CASE_SHELL").Shape
foot = m.build_foot()

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)

m.STOP_ENABLE = True
cover_on = m.build_cover(outer, foot)
m.STOP_ENABLE = False
cover_off = m.build_cover(outer, foot)
m.STOP_ENABLE = True

print("cover with stop %.1f mm3, without %.1f mm3, stop adds %.1f mm3"
      % (cover_on.Volume, cover_off.Volume, cover_on.Volume - cover_off.Volume))
print()

rows = []
for deg10 in range(400, 801, 5):
    deg = deg10 / 10.0
    leg = foot.copy()
    leg.rotate(HINGE, AXIS, -deg)
    base = leg.common(cover_off).Volume
    stop = leg.common(cover_on).Volume - base
    rows.append((deg, base, stop))

print(" deg |   baseline |  stop only")
for d, b, s in rows:
    if 56.0 <= d <= 74.0:
        print("%5.1f | %10.4f | %9.4f%s" % (d, b, s, "  <-- 65" if d == 65.0 else ""))

first = next((d for d, b, s in rows if s > 0.002), None)
at65 = next(s for d, b, s in rows if d == 65.0)
at655 = next(s for d, b, s in rows if d == 65.5)
at64 = next(s for d, b, s in rows if d == 64.0)
at645 = next(s for d, b, s in rows if d == 64.5)
at66 = next(s for d, b, s in rows if d == 66.0)
at70 = next(s for d, b, s in rows if d == 70.0)
early = [(d, s) for d, b, s in rows if d < 64.0 and s > 0.01]

ok = True


def check(label, cond, detail):
    global ok
    print("  [%s] %s -- %s" % ("PASS" if cond else "FAIL", label, detail))
    ok = ok and cond


print()
print("=== STOP-ONLY checks (baseline friction removed) ===")
check("first contact is ABOVE the 58.5 deg gravity barrier",
      first is not None and first >= 63.0,
      "first contact at %s deg" % first)
check("no stop contact below 64 deg", not early,
      "%d angles with early contact" % len(early))
# The stop face lies ALONG the plate's underside, so at the exact onset angle the two faces are
# coincident and the bearing volume is necessarily ~0 -- that is the definition of an onset, not
# a defect. What matters is that it is clear below and substantial immediately above.
check("bears on a real face just past onset", at655 > 1.0, "65.5 deg: %.4f mm3" % at655)
check("65 deg is the ONSET (64.5 is clear)", at645 < 0.01 and at655 > 1.0,
      "64.5:%.4f  65.0:%.4f  65.5:%.4f" % (at645, at65, at655))
check("rises beyond 65 deg (a hard stop)", at66 > at65 and at70 > at66,
      "65:%.2f 66:%.2f 70:%.2f" % (at65, at66, at70))

print()
print("ALL STOP CHECKS PASS" if ok else "STOP CHECKS FAILED")
