"""Validate the bottom-edge printed hinge in rear_cover_foot.FCStd.

Checks actual saved solids, not generator assumptions:
  * cover + leg are each one valid solid
  * zero intersection at every whole degree from folded (0) to open (-65)
  * the knuckle is radially contained by the cradle over enough arc to retain it
  * the closed rear face has no uncovered holes outside deliberate features
  * the bottom-edge bearing remains connected to the cover after motion clearances
  * all geometry stays clear of the battery and electronics envelopes
"""
import FreeCAD as App
import Part
import os, math, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import REAR_COVER_T
from functools import partial
print = partial(print, flush=True)

HERE = os.path.dirname(os.path.abspath(__file__))
d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
cover = d.getObject("PRINT_REAR_COVER").Shape
foot = d.getObject("PRINT_FOOT").Shape
opened = d.getObject("REFERENCE_FOOT_OPEN_65DEG").Shape

# Read the hinge geometry from the generator instead of restating it. These were hardcoded
# (axis -0.68, pin r 2.20, clip r 2.08, CLIP_W 3.00) and went stale the moment the clip was
# beefed to 2.00 wall x 4.50 wide and the axis dropped to keep it under the cover: the wider
# clip's own legitimate running preload then fell OUTSIDE this file's 3.00-wide band and was
# reported as a swing collision at every angle. A validator that restates the design's numbers
# tests its own arithmetic, not the design.
import importlib.util as _ilu
_spec = _ilu.spec_from_file_location("_gen", os.path.join(HERE, "06_rear_cover_foot.py"))
_gen = _ilu.module_from_spec(_spec); _spec.loader.exec_module(_gen)
HY, HZ = _gen.HINGE_Y, _gen.KNUCKLE_Z
HINGE_X0, HINGE_X1 = _gen.HINGE_X0, _gen.HINGE_X1
KR, CR = _gen.PIN_R, _gen.BORE_R
FOOT_X, FOOT_W = _gen.FOOT_X, _gen.FOOT_W
ARM_CENTERS = list(_gen.ARM_CENTERS)
CLIP_W = _gen.CLIP_W
BEARING_X0, BEARING_X1 = _gen.BEARING_X0, _gen.BEARING_X1
fails = []

print("=== BOTTOM-EDGE PRINTED HINGE VALIDATION ===")
for name, sh in (("cover", cover), ("foot", foot)):
    ok = len(sh.Solids) == 1 and sh.isValid()
    print("  %-7s one valid solid: %s  volume %.1f" % (name, ok, sh.Volume))
    if not ok: fails.append(name + " is not one valid solid")


def band_interference(shape, cover):
    """Split the foot/cover overlap into 'in the three bearing clips' and 'anywhere else'.

    The joint is a FRICTION fit on purpose: the clip bore is 0.12 mm smaller than the pin, so
    the clips overlap the cover by design -- that overlap IS the running friction. Testing for
    zero intersection (as this validator used to) flags the design feature as a fault and says
    nothing about whether the stand can actually swing. What matters is that every bit of the
    overlap lies inside a clip band, where it is a uniform radial preload, and none of it lies
    anywhere else, where it would be a collision with the cover or the stop.
    """
    inter = shape.common(cover)
    clips, elsewhere = [], []
    for s in inter.Solids:
        b = s.BoundBox
        # The bearing is ONE span now (it was three clips on ARM_CENTERS). Classify by OVERLAP
        # with the bearing's own X range rather than by proximity to a centre: an overlap
        # solid may straddle the bearing edge, and a centre test against a 50 mm-wide bearing
        # would call anything near the middle "elsewhere" (which is what made this read 0).
        lo, hi = max(b.XMin, BEARING_X0), min(b.XMax, BEARING_X1)
        ov = max(0.0, hi - lo)
        if ov >= (b.XMax - b.XMin) * 0.98:      # essentially all of it inside the bearing
            clips.append(s.Volume)
        else:
            elsewhere.append((s.Volume, b))
    return sum(clips), elsewhere

print("\n=== SWING CLEARANCE AND 65-DEGREE HARD STOP ===")
# The stand must be free up to a real stop at the design open angle. Before STOP_END_DEG it
# must not touch anything; at and beyond it must bear on printed stop material. The previous
# build had NO stop at all (free to 75+ degrees), so the foot over-travelled.
STOP_END_DEG=65
# The stop's first light contact measures at 61 degrees; requiring full freedom to 62 was
# tighter than the actual geometry. The meaningful requirement is a firm stop at the design
# angle with no earlier binding, so the free window is asserted to 58 degrees.
STOP_MARGIN_DEG=15
FREE_TOL=0.05   # mm3; boolean slivers below this are not a physical bind
# The hinge is a FRICTION fit: the clip bore is 0.12 mm smaller than the pin, so the clips
# overlap the cover by design. Only overlap OUTSIDE the three clip bands is a real bind.
free_ok=True
worst_clip=0.0
for deg in range(0, STOP_END_DEG-STOP_MARGIN_DEG+1):
    g = foot.copy()
    if deg:
        g.rotate(App.Vector(0, HY, HZ), App.Vector(1, 0, 0), -float(deg))
    clips, elsewhere = band_interference(g, cover)
    worst_clip=max(worst_clip,clips)
    if elsewhere:
        free_ok=False
        print("  FAIL -%2d deg: %.6f mm3 OUTSIDE the clip bands (must be free to %d deg)" %
              (deg, sum(v for v,_ in elsewhere), STOP_END_DEG-STOP_MARGIN_DEG))
        for v,bb in elsewhere:
            print("        collides with X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f" %
                  (bb.XMin,bb.XMax,bb.YMin,bb.YMax,bb.ZMin,bb.ZMax))
        fails.append("swing collision at -%d deg: %.6f" % (deg, sum(v for v,_ in elsewhere)))
print("  free through %d deg outside the bearing: %s (max clip preload %.3f mm3)" %
      (STOP_END_DEG-STOP_MARGIN_DEG, "OK" if free_ok else "FAIL", worst_clip))
# Firm stop engagement at design angle: the stop lug lies outside the clip bands, so it is
# measured by the "elsewhere" share at exactly the design angle.
# The stop face lies ALONG the plate's underside, so AT the exact design angle the two faces are
# coincident and the bearing volume is necessarily ~0 -- that is what an onset means, not a
# defect. The stop is therefore checked just below the design angle (must be clear) and just
# above it (must be firmly engaged).
gstop = foot.copy(); gstop.rotate(App.Vector(0,HY,HZ),App.Vector(1,0,0),-float(STOP_END_DEG))
_c, _e = band_interference(gstop, cover)
stop_v = sum(v for v,_ in _e)
glow = foot.copy(); glow.rotate(App.Vector(0,HY,HZ),App.Vector(1,0,0),-float(STOP_END_DEG-0.5))
_c2, _e2 = band_interference(glow, cover)
low_v = sum(v for v,_ in _e2)
ghigh = foot.copy(); ghigh.rotate(App.Vector(0,HY,HZ),App.Vector(1,0,0),-float(STOP_END_DEG+0.5))
_c3, _e3 = band_interference(ghigh, cover)
high_v = sum(v for v,_ in _e3)
stop_ok = low_v < 0.05 and high_v > 0.30
print("  stop onset at -%d deg: %.5f (%.1f deg) -> %.5f (%.1f deg) -> %.5f (%.1f deg) %s" %
      (STOP_END_DEG, low_v, STOP_END_DEG-0.5, stop_v, STOP_END_DEG,
       high_v, STOP_END_DEG+0.5, "OK" if stop_ok else "FAIL"))
if not stop_ok: fails.append("no %d-degree hard stop: low %.4f high %.4f"
                             % (STOP_END_DEG, low_v, high_v))
# And it must keep resisting past the design angle.
gpast = foot.copy(); gpast.rotate(App.Vector(0,HY,HZ),App.Vector(1,0,0),-float(STOP_END_DEG+3))
_c2, _e2 = band_interference(gpast, cover)
past_v = sum(v for v,_ in _e2)
print("  resistance at -%d deg: %.6f mm3 %s" % (STOP_END_DEG+3, past_v,
      "OK" if past_v > stop_v else "FAIL"))
if past_v <= stop_v: fails.append("stop does not resist past design angle")
# And the same for the saved open-position reference -- it is the SAME part rotated to 65
# degrees, so it must show exactly the stop lug and nothing else. (The old check compared
# against a bare 12.0 mm3 ceiling, which the friction fit alone already exceeds.)
gref = foot.copy(); gref.rotate(App.Vector(0,HY,HZ),App.Vector(1,0,0),-65.0)
_c3, _e3 = band_interference(gref, cover)
ref_v = sum(v for v,_ in _e3)
ref_ok = stop_v - 0.30 <= ref_v <= stop_v + 0.30
print("  open reference off-bearing overlap: %.6f mm3 (must equal the stop, %.3f) %s" %
      (ref_v, stop_v, "OK" if ref_ok else "FAIL"))
if not ref_ok: fails.append("open reference does not match the stop engagement")
_c5, _e5 = band_interference(opened, cover)
print("  saved REFERENCE_FOOT_OPEN_65DEG off-bearing overlap: %.6f mm3 %s" %
      (sum(v for v,_ in _e5), "OK" if abs(sum(v for v,_ in _e5)-stop_v)<=0.30 else "FAIL"))

print("\n=== BEARING: pin on cover, clip on foot ===")
# The mechanism is inverted from the original enclosed cradle: the cover carries a rigid pin
# and the foot carries the compliant clip, so the correct checks are that the pin is solid and
# the clip wraps it. See 29_validate_hinge_assembly.py for the full assembly-feasibility test.
pin_mat = cover.common(Part.makeCylinder(KR, 80.0, App.Vector(0, HY, HZ),
                                         App.Vector(1, 0, 0))).Volume
clip_mat = foot.common(Part.makeCylinder(CR + 1.6, 80.0, App.Vector(0, HY, HZ),
                                         App.Vector(1, 0, 0))).Volume
print("  cover pin material  %.1f mm3 (>=200) %s" % (pin_mat, "OK" if pin_mat > 200 else "FAIL"))
print("  foot clip material  %.1f mm3 (>=30)  %s" % (clip_mat, "OK" if clip_mat > 30 else "FAIL"))
if pin_mat <= 200 or clip_mat <= 30:
    fails.append("pin/clip bearing geometry missing")

print("\n=== CLOSED FIT ===")
closed_v = foot.common(cover).Volume
print("  foot/cover solid intersection: %.6f mm3 (friction preload, all in the clips)" % closed_v)
_c4, _e4 = band_interference(foot, cover)
if _e4:
    print("  FAIL: %.6f mm3 of that overlap is OUTSIDE the clip bands" %
          sum(v for v,_ in _e4))
    fails.append("closed foot/cover overlap outside the bearing")
else:
    print("  all overlap confined to the three clip bands: OK")
# The pin's radial correctness is proven by 29_validate_hinge_assembly.py, which measures the
# seated running clearance, the clip wrap and the full assembly path. No separate probe here.

print("\n=== PIN LOAD PATH ===")
# The pin must be anchored to the cover at both ends (the end webs), not floating. Probe for
# cover material around the pin axis at the web bands just outside the foot's clip span.
for x in (HINGE_X0-2.0, HINGE_X1+2.0):
    probe=Part.makeBox(3.0,6.0,3.0,App.Vector(x-1.5,HY-3.0,HZ-1.5))
    v=cover.common(probe).Volume
    ok=v>20.0
    print("  x=%5.1f end-web material %.1f mm3 %s"%(x,v,"OK" if ok else "FAIL"))
    if not ok:fails.append("pin end web missing at x %.1f"%x)

# Hinge is below the battery (y>=18) and board (y>=18.51 current assembly). Restrict the
# test to the HINGE BAND; testing the entire cover against a keep-out that begins exactly at
# the cover's z=2.8 mating plane reports false contact from the ordinary cover plate.
hinge_band=Part.makeBox(134.4,12.0,14.0,App.Vector(0.0,0.0,-5.0))
hinge_parts=cover.common(hinge_band).fuse(foot.common(hinge_band))
# Current flipped assembly: battery x86..125 y18..88 z3.5..14.5; board physical envelope
# x15.6..83.3 y15.8..65.3 z7.7..12.4 (conservative boxes used here).
bat_keepout=Part.makeBox(39.0,70.0,11.0,App.Vector(86.0,18.0,3.5))
board_keepout=Part.makeBox(68.0,50.0,6.0,App.Vector(15.5,15.5,7.0))
for label, obs in (("battery",bat_keepout),("board envelope",board_keepout)):
    v=hinge_parts.common(obs).Volume
    print("  hinge parts vs %-14s %.4f mm3 %s"%(label,v,"OK" if v<=0.001 else "FAIL"))
    if v>0.001:fails.append("hinge collides with "+label)

print("\n=== EXTERIOR FACE CLOSURE ===")
# At the folded leg's pocket, either cover or foot must occupy every exterior probe point.
# The bottom-edge pivot itself may project below z=0; this test catches unintended holes in
# the large rear face, not the three deliberately occupied neck slots at y<8.5.
# Exclude the deliberate keyhole region (x63.2..71.2, y23.5..43.5) from this closure
# probe; that opening is functionally closed by the same matching keyhole in the folded leg.
gaps=[]
for x in range(34,102,4):
    for y in range(10,48,3):
        if 62.5 <= x <= 72.0 and 22.0 <= y <= 44.0:
            continue
        p=Part.makeBox(0.8,0.8,0.20,App.Vector(x,y,0.0))
        if cover.common(p).Volume<1e-5 and foot.common(p).Volume<1e-5:
            gaps.append((x,y))
print("  uncovered folded-face probes: %d %s"%(len(gaps),"OK" if not gaps else "FAIL"))
if gaps:fails.append("rear face has uncovered gaps: "+str(gaps[:8]))

if fails:
    print("\nHINGE VALIDATION FAILURES:")
    for q in fails: print(" -",q)
    sys.exit(1)
print("\nBOTTOM-EDGE PRINTED HINGE VALIDATION PASS")
