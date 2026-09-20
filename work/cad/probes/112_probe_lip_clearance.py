"""Sweep the leg and track the MOUTH LIPS' clearance to the cover.

This is the measurement that was missing. The old "margin" was ONE scalar
(CAVITY_CLEAR - preload - nub), which assumes the clip expands uniformly. It does not:
a C-clip is open at the mouth, so pushing the ring out anywhere opens it AT THE LIPS,
and the lips are the last material to clear the cover as the leg swings.

So this tracks, per swing angle:
  - overlap of the clip with the cover (must only ever be the pin interference band),
  - the closest approach of the LIPS to any cover material,
  - the closest approach of the rest of the ring (the wrap) to cover material.

A flare would show up as the lip clearance collapsing relative to the wrap clearance.

Env: SRC, CX (clip station).
"""
import math
import os

import FreeCAD as App
import Part

SRC = os.environ.get("SRC", "rear_cover_foot.FCStd")
CX = float(os.environ.get("CX", "67.2"))

doc = App.openDocument(SRC)
foot = doc.getObject("PRINT_FOOT").Shape
cover = doc.getObject("PRINT_REAR_COVER").Shape

import importlib
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
m = importlib.import_module("06_rear_cover_foot")
MOUTH_OPEN_DEG = float(os.environ.get("MOUTH", "150.0"))

HY, KZ = m.HINGE_Y, m.KNUCKLE_Z
HINGE = App.Vector(0, HY, KZ)
AXIS = App.Vector(1, 0, 0)

# The pin is rigid cover material that the clip is SUPPOSED to grip; take it out of the
# "cover" the clip must clear, or every reading is a false zero.
pin_band = Part.makeCylinder(m.PIN_R, (m.HINGE_X1 - m.HINGE_X0) + 2 * m.PIN_ROOT,
                             App.Vector(m.HINGE_X0 - m.PIN_ROOT, HY, KZ), App.Vector(1, 0, 0))
cover_open = cover.cut(pin_band)

# The clip's own solid at this station, minus the pin: ring band the foot owns, and cut away
# whatever lies inside the pin radius so only the clip wall and lips remain.
ring = Part.makeCylinder(m.BORE_R + m.CLIP_WALL + 0.6, m.CLIP_W,
                         App.Vector(CX - m.CLIP_W / 2, HY, KZ), App.Vector(1, 0, 0)).cut(
       Part.makeCylinder(m.PIN_R, m.CLIP_W + 0.2,
                         App.Vector(CX - m.CLIP_W / 2 - 0.1, HY, KZ), App.Vector(1, 0, 0)))
clip = foot.common(ring)

# Mouth lips: two small pads straddling each edge of the opening, placed at the OUTER wall so
# they measure the same flare that would catch on the cover.
half = math.radians(MOUTH_OPEN_DEG / 2.0)
lip_a = [math.radians(m.MOUTH_PIN_ANGLE) - half, math.radians(m.MOUTH_PIN_ANGLE) + half]


def pad(ang):
    r = m.BORE_R + m.CLIP_WALL        # ON the clip's outer wall
    y = HY + r * math.sin(ang)
    z = KZ + r * math.cos(ang)
    return Part.makeBox(0.4, 0.3, 0.3, App.Vector(CX - 0.2, y - 0.15, z - 0.15))


lips = [pad(a) for a in lip_a]

print("clip station x=%.2f   mouth pin angle %.0f deg  opening %.0f deg"
      % (CX, m.MOUTH_PIN_ANGLE, MOUTH_OPEN_DEG))
print()
print(" swing | clip-vs-cover overlap | lip gap | wrap gap")
worst = None
for k in range(0, 76, 1):
    leg = foot.copy()
    leg.rotate(HINGE, AXIS, -float(k))
    ov = leg.common(cover)
    ovv = ov.Volume
    # lip gap: nearest cover material to each lip pad, restricted to the cavity band
    lipgap = 9e9
    for p in lips:
        pp = p.copy()
        pp.rotate(HINGE, AXIS, -float(k))
        d = pp.distToShape(cover_open)
        if d[0] < lipgap:
            lipgap = d[0]
    # wrap gap: same but for a pad on the wrap, 120 deg from the mouth
    wp = pad(math.radians(m.MOUTH_PIN_ANGLE + 180.0))
    wp.rotate(HINGE, AXIS, -float(k))
    wrapgap = wp.distToShape(cover_open)[0]
    print("  %3d  | %10.4f mm3        | %6.3f  | %6.3f" % (k, ovv, lipgap, wrapgap))
    if worst is None or lipgap < worst[1]:
        worst = (k, lipgap, wrapgap, ovv)
print()
print("closest the LIPS ever come to the cover: %.3f mm at %.0f deg (wrap gap there %.3f)"
      % (worst[1], worst[0], worst[2]))
if worst[1] < 0.15:
    print("WARNING: lip clearance under 0.15 mm -- a flare or a print error will catch here")
else:
    print("OK: lips keep clearance at every swing angle")
