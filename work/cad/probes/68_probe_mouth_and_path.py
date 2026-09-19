"""Settle the hinge geometry empirically.

1. Where is the clip MOUTH? Sample a ring at mid-wall radius around the pin axis and report
   which pin angles are empty (the mouth) -- at folded and at 65 deg open.
2. Is there ANY motion that seats the foot on the pin? Sweep a family of paths.
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
HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
CX = m.ARM_CENTERS[1]

print("=== 1. CLIP MOUTH ANGULAR POSITION ===")
print("pin angle convention: 0 = +Z, 90 = +Y, 180 = -Z, 270 = -Y")


def mouth_at(shape, r_sample):
    empty = []
    for i in range(360):
        a = math.radians(i)
        y = HY + r_sample * math.sin(a)
        z = HZ + r_sample * math.cos(a)
        b = Part.makeBox(0.2, 0.2, 0.2, App.Vector(CX - 0.1, y - 0.1, z - 0.1))
        if shape.common(b).Volume <= 0.004:
            empty.append(i)
    return empty


for r_s in (2.4, 2.9, 3.4):
    e = mouth_at(foot, r_s)
    if e:
        print("  r %.1f folded: %3d empty angles, %d..%d (centre %.0f)"
              % (r_s, len(e), e[0], e[-1], e[len(e) // 2]))
    else:
        print("  r %.1f folded: NO opening" % r_s)

leg65 = foot.copy()
leg65.rotate(HINGE, AXIS, -65.0)
for r_s in (2.9,):
    e = mouth_at(leg65, r_s)
    if e:
        print("  r %.1f at 65 open: %3d empty angles, %3d..%3d (centre %.0f)"
              % (r_s, len(e), e[0], e[-1], e[len(e) // 2]))

print("\n=== 2. IS THERE A SEATING MOTION? ===")
m.STOP_ENABLE = False
cover = m.build_cover(outer, foot)
m.STOP_ENABLE = True
print("testing WITHOUT the ramps, so only the cover's own material can block.\n")


def clash(shape):
    return shape.common(cover).Volume


print("(a) pure translations")
for axis, rng in (("Z", (-40, 40, 4)), ("Y", (-40, 40, 4)), ("X", (-40, 40, 4))):
    row = []
    for v in range(rng[0], rng[1], rng[2]):
        g = foot.copy()
        if axis == "Z":
            g.translate(App.Vector(0, 0, v))
        elif axis == "Y":
            g.translate(App.Vector(0, v, 0))
        else:
            g.translate(App.Vector(v, 0, 0))
        row.append("%s%d:%.1f" % (axis, v, clash(g)))
    print("   " + "  ".join(row))

print("\n(b) swing the foot OPEN (about the pin) -- does the plate clear the cover?")
for deg in (0, 20, 40, 60, 70, 80, 90, 100, 110, 120):
    g = foot.copy()
    g.rotate(HINGE, AXIS, -float(deg))
    print("   open %3d deg : clash %8.3f mm3" % (deg, clash(g)))
