"""Measure the A-frame stop: weld area, first-contact angle, bearing volume, and root bearing.

This is the check that matters. The user rejected the previous stop because its attachment was
a 0.7 mm3 sliver; so the acceptance test is (a) a real root bearing area on the webs and
(b) a weld that is a genuine volume, not a sliver.
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

m.STOP_ENABLE = False
cover_off = m.build_cover(outer, foot)
m.STOP_ENABLE = True
cover_on = m.build_cover(outer, foot)

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
CXS = m.ARM_CENTERS

print("cover WITHOUT stop %.1f mm3 ; WITH %.1f mm3 ; stop adds %.1f mm3"
      % (cover_off.Volume, cover_on.Volume, cover_on.Volume - cover_off.Volume))


def slabs_of(inter):
    out = []
    for s in inter.Solids:
        out.append(s)
    return out


print("\n1. STOP WELD -- the stop geometry intersected with the cover as it was WITHOUT it")
ca, sa = math.cos(math.radians(m.SWING_DEG)), math.sin(math.radians(m.SWING_DEG))
py, pz = m.STOP_FACE_P
ex, ez = py + m.STOP_FACE_LEN*ca, pz - m.STOP_FACE_LEN*sa
ny, nz = -sa, -ca
T = m.STOP_TIE_T


def quad_x(pts2d, x0, dx):
    pts = [App.Vector(0.0, y, z) for y, z in pts2d]
    pts.append(pts[0])
    return (Part.Face(Part.makePolygon(pts)).extrude(App.Vector(dx, 0, 0))
            .translate(App.Vector(x0, 0, 0)))


face_pts = [(py, pz), (ex, ez), (ex + T*ny, ez + T*nz), (py + T*ny, pz + T*nz)]
stop = quad_x(face_pts, m.FOOT_X, m.FOOT_W)
for x0, x1 in m.STOP_STRUTS:
    sp = [(m.STOP_STRUT_Y0, m.WEB_Z1), (ex, ez),
          (ex + T*ny, ez + T*nz), (m.STOP_STRUT_Y0, m.WEB_Z1 + T*nz)]
    stop = stop.fuse(quad_x(sp, x0, x1-x0))
print("   stop geometry volume %.2f mm3" % stop.Volume)
joint = stop.common(cover_off)
print("   stop OVERLAPS the pre-existing cover by %.2f mm3" % joint.Volume)
for s in sorted(joint.Solids, key=lambda x: -x.Volume):
    bb = s.BoundBox
    print("      %7.2f mm3  X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\n2. ROOT BEARING AREA on the web tops (z = %.2f)" % m.WEB_Z1)
for x0, x1 in m.STOP_STRUTS:
    a = (x1-x0) * (ez - m.STOP_STRUT_Y0) if False else None
    # measured: slice the strut at the web top
    sec = stop.common(Part.makeBox(400, 400, 0.05,
                                   App.Vector(-100, -100, m.WEB_Z1)))
    bb = sec.BoundBox
    print("   strut X %.2f..%.2f : section at web top = %.2f mm2"
          % (x0, x1, sec.Volume / 0.05))

print("\n3. FIRST CONTACT vs angle (foot vs cover WITH stop, minus the no-stop baseline)")


def offbearing(shape):
    inter = shape.common(cover_on)
    base = shape.common(cover_off)
    tot = inter.Volume - base.Volume
    return max(tot, 0.0), inter.Volume, base.Volume


for deg in (55, 58, 60, 62, 63, 64, 64.5, 65, 65.5, 66, 67, 68, 70, 75, 90):
    f = foot.copy()
    f.rotate(HINGE, AXIS, -float(deg))
    ob, tot, base = offbearing(f)
    print("   %5.1f deg : stop-only contact %9.4f mm3   (total %8.3f, friction baseline %7.3f)"
          % (deg, ob, tot, base))

print("\n4. CLIP SWEEP CLEARANCE -- the struts must not foul the swinging clips")
for deg in (0, 10, 20, 30, 40, 50, 55, 60, 65, 70):
    f = foot.copy()
    f.rotate(HINGE, AXIS, -float(deg))
    cb = 0.0
    for c in CXS:
        sl = Part.makeBox(3.4, 300, 300, App.Vector(c-1.7, -60, -100))
        cb += f.common(cover_on).common(sl).Volume
        cb -= f.common(cover_off).common(sl).Volume
    print("   %3d deg : clip-band contact with the stop %7.4f mm3" % (deg, max(cb, 0.0)))
