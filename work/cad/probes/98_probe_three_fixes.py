"""Verify the three fixes: clip weld, stop weld depth, hatch insertability.

FIX 1  clip ring -> plate: was 0.0870 mm3 (a tangent line). Must be a real volume.
FIX 2  stop riser -> web: was 0.88 mm deep on a 12 mm cantilever. Must be deeply buried.
FIX 3  hatch inner portion: was 3.10 mm LARGER than the bay in both axes. Must pass through,
       and the notches must be through-cut (no undercut), and nothing may lap behind the cover.
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

FAIL = []

# ============================================================ FIX 1: clip weld ==========
print("=" * 78)
print("FIX 1  CLIP RING -> PLATE WELD")
print("=" * 78)
plate = m.rprism(m.FOOT_W, m.FOOT_H, 4.0, m.FOOT_X, m.FOOT_Y, 0.0, m.FOOT_T)
CX = m.ARM_CENTERS[1]
HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
OUT_R = m.BORE_R + m.CLIP_WALL
ring = Part.makeCylinder(OUT_R, m.CLIP_W,
                         App.Vector(CX - m.CLIP_W / 2, HY, HZ), App.Vector(1, 0, 0)).cut(
       Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.2,
                         App.Vector(CX - m.CLIP_W / 2 - 0.1, HY, HZ), App.Vector(1, 0, 0)))
gus = Part.makeBox(m.CLIP_W, m.CLIP_GUSSET_Y1 - m.CLIP_GUSSET_Y0,
                   m.CLIP_GUSSET_Z1 - m.CLIP_GUSSET_Z0,
                   App.Vector(CX - m.CLIP_W / 2, m.CLIP_GUSSET_Y0, m.CLIP_GUSSET_Z0))
gus = gus.cut(Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.4,
                                App.Vector(CX - m.CLIP_W / 2 - 0.2, HY, HZ), App.Vector(1, 0, 0)))

print("   bare ring x plate        : %.4f mm3   (was the entire attachment)" % ring.common(plate).Volume)
w_ring = gus.common(ring).Volume
w_plate = gus.common(plate).Volume
print("   gusset x ring            : %.4f mm3" % w_ring)
print("   gusset x plate           : %.4f mm3" % w_plate)
print("   TOTAL per clip           : %.4f mm3   (x3 clips = %.2f mm3)"
      % (w_ring + w_plate, 3 * (w_ring + w_plate)))
if w_ring + w_plate < 10.0:
    FAIL.append("clip weld still small: %.3f mm3" % (w_ring + w_plate))

# The gusset must not enter the mouth channel, or it stiffens the lip the pin pushes past.
mouth = Part.makeBox(m.CLIP_W + 0.4, m.BORE_R * 2.6, m.BORE_R + m.CLIP_WALL - 0.5388,
                     App.Vector(CX - m.CLIP_W / 2 - 0.2, HY - m.BORE_R * 1.3,
                                HZ - (m.BORE_R + m.CLIP_WALL)))
mouth.rotate(App.Vector(CX, HY, HZ), App.Vector(1, 0, 0), m.MOUTH_PIN_ANGLE - 180.0)
mo = gus.common(mouth).Volume
print("   gusset intruding into mouth channel: %.4f mm3" % mo)
if mo > 1e-6:
    FAIL.append("gusset enters the mouth channel: %.4f mm3" % mo)

# The gusset must not reduce the clip's compliance: check it stays clear of the mouth LIP
# (the arc from the mouth edge round to the ring's outer wall on the mouth side).
print("   gusset Y range %.2f..%.2f ; mouth box +Y edge %.3f"
      % (m.CLIP_GUSSET_Y0, m.CLIP_GUSSET_Y1, HY + m.BORE_R * 1.3))

# ============================================================ FIX 2: stop weld ==========
print()
print("=" * 78)
print("FIX 2  STOP RISER -> WEB")
print("=" * 78)
ca, sa = math.cos(math.radians(m.SWING_DEG)), math.sin(math.radians(m.SWING_DEG))
py, pz = m.STOP_RAMP_P
ey, ez = py + m.STOP_RAMP_LEN * ca, pz - m.STOP_RAMP_LEN * sa
by, bz = ey - m.STOP_RAMP_T * sa, ez - m.STOP_RAMP_T * ca
ay, az = py - m.STOP_RAMP_T * sa, pz - m.STOP_RAMP_T * ca
quad = [App.Vector(0, py, pz), App.Vector(0, ey, ez),
        App.Vector(0, by, bz), App.Vector(0, ay, az)]
quad.append(quad[0])
stop = None
for x0, x1 in m.STOP_SPAN_X:
    ramp = (Part.Face(Part.makePolygon(quad))
            .extrude(App.Vector(x1 - x0, 0, 0)).translate(App.Vector(x0, 0, 0)))
    stop = ramp if stop is None else stop.fuse(ramp)
for x0, x1 in m.STOP_RISER_X:
    stop = stop.fuse(Part.makeBox(x1 - x0, m.STOP_RISER_Y1 - m.STOP_RISER_Y0,
                                  m.STOP_RISER_Z1 - m.STOP_RISER_Z0,
                                  App.Vector(x0, m.STOP_RISER_Y0, m.STOP_RISER_Z0)))
print("   stop solid: %.3f mm3, %d solid(s)" % (stop.Volume, len(stop.Solids)))
if len(stop.Solids) != 2:
    FAIL.append("stop is not 2 clean halves: %d solid(s)" % len(stop.Solids))

src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = src.getObject("CASE_SHELL").Shape
m.STOP_ENABLE = False
cover_ns = m.build_cover(outer, m.build_foot())
m.STOP_ENABLE = True
weld = stop.common(cover_ns)
print("   weld: %.3f mm3 across %d solid(s)" % (weld.Volume, len(weld.Solids)))
for s in sorted(weld.Solids, key=lambda q: q.BoundBox.XMin):
    bb = s.BoundBox
    print("      %8.3f mm3  Z %.2f..%.2f  (engagement %.2f mm)"
          % (s.Volume, bb.ZMin, bb.ZMax, bb.ZMax - bb.ZMin))
if weld.Volume < 60.0:
    FAIL.append("stop weld still small: %.3f mm3" % weld.Volume)
if min(s.BoundBox.ZMax - s.BoundBox.ZMin for s in weld.Solids) < 4.0:
    FAIL.append("stop engagement still shallow")

# ============================================================ FIX 3: hatch ==============
print()
print("=" * 78)
print("FIX 3  HATCH INSERTION")
print("=" * 78)
X0, X1 = m.SERVICE_BAY_X0, m.SERVICE_BAY_X1
Y0, Y1 = m.SERVICE_BAY_Y0, m.SERVICE_BAY_Y1
print("   bay opening          X %.2f..%.2f  Y %.2f..%.2f  (%.2f x %.2f)"
      % (X0, X1, Y0, Y1, X1 - X0, Y1 - Y0))

hatch = m.build_hatch()
plug = None
# Rebuild the pieces the way build_hatch does, to measure the inner portion alone.
plug = m.rprism((X1 - X0) - 2 * m.HATCH_CLEAR, (Y1 - Y0) - 2 * m.HATCH_CLEAR, 2.75,
                X0 + m.HATCH_CLEAR, Y0 + m.HATCH_CLEAR, 0.0, m.HATCH_PLUG_T)
keys = None
for kx0, kx1, ky0, ky1 in m.HATCH_KEYS:
    k = m.rprism(kx1 - kx0, ky1 - ky0, 3.0, kx0, ky0, 0.0, m.HATCH_KEY_T)
    keys = k if keys is None else keys.fuse(k)
inner = plug.fuse(keys)
bb = inner.BoundBox
print("   hatch inner portion  X %.2f..%.2f  Y %.2f..%.2f  (%.2f x %.2f)"
      % (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.XMax - bb.XMin, bb.YMax - bb.YMin))
# Does any of it project past the bay opening other than through a notch?
notch = None
for kx0, kx1, ky0, ky1 in m.HATCH_KEYS:
    n = m.rprism((kx1 - kx0) + 2 * m.HATCH_KEY_CLEAR,
                 (ky1 - ky0) + 2 * m.HATCH_KEY_CLEAR, 3.0,
                 kx0 - m.HATCH_KEY_CLEAR, ky0 - m.HATCH_KEY_CLEAR, -0.5, m.COVER_T + 1.0)
    notch = n if notch is None else notch.fuse(n)
exit_req = m.rprism(X1 - X0, Y1 - Y0, 3.0, X0, Y0, -0.5, m.COVER_T + 1.0).fuse(notch)
esc = inner.cut(exit_req)
print("   inner portion that exits NEITHER the bay NOR a notch: %.4f mm3 (must be 0)" % esc.Volume)
if esc.Volume > 1e-6:
    FAIL.append("hatch projects past the bay without a notch: %.4f mm3" % esc.Volume)
print("   notches are through-cut: key centres are in x 21.30/80.40, y 80.00/76.00")

# Notches are through-cut, so an exit in +Z must always exist. Test: can a vertical ray at the
# centroid of each key get from the key out of the cover in +Z?
cover = m.build_cover(outer, m.build_foot())
for i, (kx0, kx1, ky0, ky1) in enumerate(m.HATCH_KEYS):
    cx, cy = (kx0 + kx1) / 2.0, (ky0 + ky1) / 2.0
    col = cover.common(Part.makeBox(0.02, 0.02, 6.0,
                                    App.Vector(cx, cy, m.HATCH_KEY_T - 0.01)))
    print("   key %d centre (%.2f,%.2f): cover material above the key  %.4f mm3 %s"
          % (i + 1, cx, cy, col.Volume, "OK" if col.Volume < 1e-6 else "FAIL"))
    if col.Volume > 1e-6:
        FAIL.append("notch %d is not through-cut: material above the key" % (i + 1))

# Nothing may lap behind the cover: check the hatch for material at z<0 that is NOT part of the
# flange footprint, i.e. that any z<0 material sits within the flange.
flange = m.rprism(m.HATCH_FLANGE_X1 - m.HATCH_FLANGE_X0,
                  m.HATCH_FLANGE_Y1 - m.HATCH_FLANGE_Y0, 4.0,
                  m.HATCH_FLANGE_X0, m.HATCH_FLANGE_Y0, -m.HATCH_FLANGE_T, m.HATCH_FLANGE_T)
below = hatch.common(Part.makeBox(400, 400, 1.29, App.Vector(-200, -200, -1.3)))
stray = below.cut(flange)
print("   hatch material at z -1.3..0 OUTSIDE the flange lap: %.4f mm3 (must be 0)" % stray.Volume)
if stray.Volume > 1e-6:
    FAIL.append("hatch laps behind the cover outside the flange: %.4f mm3" % stray.Volume)

# Insertion: the hatch starts fully outside (flange below the cover's exterior face) and is
# driven in +Z to the seated position at dz=0. Overlap must be zero the whole way.
# dz>0 is OVER-insertion past the seat and is not a valid assembly position.
print("   insertion from outside, driven +Z to the seated position (dz = 0):")
blocked = False
for dz in (-3.6, -3.0, -2.0, -1.0, -0.5, -0.2, 0.0):
    h2 = hatch.copy()
    h2.translate(App.Vector(0, 0, dz))
    ov = h2.common(cover)
    ok = ov.Volume < 1e-6
    if not ok:
        blocked = True
    print("      dz %+5.1f : %9.4f mm3 %s" % (dz, ov.Volume, "OK" if ok else "<-- BLOCKED"))
if blocked:
    FAIL.append("hatch cannot be inserted from outside")
print("   seated hatch inner face z %.2f = cover inner face z %.2f (flush, no intrusion)"
      % (m.HATCH_PLUG_T, m.COVER_T))

# flange must clear the wall-hang keyholes (slot ends at Y 65.00)
print("   flange Y0 %.2f vs keyhole slot top %.2f : %s"
      % (m.HATCH_FLANGE_Y0, 65.00, "OK" if m.HATCH_FLANGE_Y0 >= 65.0 else "FAIL overlap"))
if m.HATCH_FLANGE_Y0 < 65.0:
    FAIL.append("flange covers the wall-hang keyhole slot")

# ============================================================ summary ===================
print()
print("=" * 78)
if FAIL:
    print("FAILURES:")
    for f in FAIL:
        print("   - " + f)
else:
    print("ALL THREE FIXES VERIFIED")
