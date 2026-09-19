"""Test the mouth-orientation fix.

Current: mouth centred at pin angle 180 (-Z, i.e. DOWN). But the only clear insertion
direction is -Z (approach from below), which presents the pin at pin angle 0 -- the solid
210 deg WRAP. So the pin is forced through the wall.

Candidate: rotate the mouth to pin angle 0 (+Z, into the case). Then a foot positioned below
(dz<0) presents the pin at the mouth and slides straight up on. -Z translation is the one
direction already measured clear.

This builds both variants and sweeps the insertion translation.
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

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
AXIS = App.Vector(1, 0, 0)
PIN_AXIS_PT = App.Vector(0, HY, HZ)
BORE_R, OUT_R, CLIP_W = m.BORE_R, m.BORE_R + m.CLIP_WALL, m.CLIP_W
DETENT_H = m.DETENT_H


def make_foot(mouth_rot_deg, rib_rot_deg=0.0):
    foot = m.rprism(m.FOOT_W, m.FOOT_H, 4.0, m.FOOT_X, m.FOOT_Y, 0.0, m.FOOT_T)
    MOUTH_OPEN_DEG = 150.0
    half_chord = BORE_R * math.sin(math.radians(MOUTH_OPEN_DEG / 2.0))
    mouth_h = (BORE_R ** 2 - half_chord ** 2) ** 0.5
    for cx in m.ARM_CENTERS:
        ring = Part.makeCylinder(OUT_R, CLIP_W,
                                 App.Vector(cx - CLIP_W / 2, HY, HZ), AXIS).cut(
               Part.makeCylinder(BORE_R, CLIP_W + 0.2,
                                 App.Vector(cx - CLIP_W / 2 - 0.1, HY, HZ), AXIS))
        mouth = Part.makeBox(CLIP_W + 0.4, BORE_R * 2.6, OUT_R - mouth_h,
                             App.Vector(cx - CLIP_W / 2 - 0.2, HY - BORE_R * 1.3,
                                        HZ - OUT_R))
        if mouth_rot_deg:
            mouth.rotate(App.Vector(cx, HY, HZ), AXIS, float(mouth_rot_deg))
        ring = ring.cut(mouth)
        rib = Part.makeCylinder(DETENT_H, CLIP_W - 2 * m.CLIP_WALL,
                                App.Vector(cx - CLIP_W / 2 + m.CLIP_WALL,
                                           HY + (BORE_R - DETENT_H / 2.0), HZ), AXIS)
        ring = ring.fuse(rib)
        foot = foot.fuse(ring)
    rib_bottom = HZ - OUT_R
    pr = Part.makeBox(m.FOOT_W - 2 * m.RIB_INSET, m.PRINT_RIB_Y, m.FOOT_T - rib_bottom,
                      App.Vector(m.FOOT_X + m.RIB_INSET,
                                 m.FOOT_Y + m.FOOT_H - m.PRINT_RIB_Y, rib_bottom))
    return foot.fuse(pr).removeSplitter()


def pin_angle(dz):
    """Where the pin sits relative to the bore centre, in degrees (0=+Z, 90=+Y, 180=-Z)."""
    dy, dzz = HY - HY, HZ - (HZ + dz)
    return math.degrees(math.atan2(0.0, dzz)) if dy == 0 else 0.0


def offbearing(shape, cover):
    inter = shape.common(cover)
    tot = inter.Volume
    for c in m.ARM_CENTERS:
        s = Part.makeBox(3.4, 300, 300, App.Vector(c - 1.7, -60, -100))
        tot -= inter.common(s).Volume
    return max(tot, 0.0), inter.Volume


for label, rot in (("CURRENT (mouth at pin angle 180 = -Z)", 0.0),
                   ("CANDIDATE (mouth at pin angle 0 = +Z)", 180.0)):
    print("\n" + "=" * 78)
    print(label)
    print("=" * 78)
    try:
        foot = make_foot(rot)
    except Exception as e:
        print("   build failed:", e)
        continue
    print("   foot solids %d valid %s vol %.1f" % (len(foot.Solids), foot.isValid(),
                                                   foot.Volume))
    m.STOP_ENABLE = False
    cover = m.build_cover(outer, foot)
    print("   dz   pin-angle  off-bearing   total-interf   note")
    for dz in range(10, -9, -1):
        f = foot.copy()
        f.translate(App.Vector(0, 0, dz))
        ob, tot = offbearing(f, cover)
        pa = math.degrees(math.atan2(0.0, HZ - (HZ + dz)))
        pa = 0.0 if dz < 0 else 180.0
        note = ""
        if ob > 0.05:
            note = "OFF-BEARING BLOCK"
        print("   %+3d   %8s   %9.3f   %11.3f   %s"
              % (dz, "0 (+Z)" if dz < 0 else "180 (-Z)", ob, tot, note))
