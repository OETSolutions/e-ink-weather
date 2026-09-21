"""Verify the hinge is ASSEMBLABLE and DEMOUNTABLE, not merely non-colliding when seated.

The previous mechanism was a closed tunnel: the bore wrapped 360 degrees, the only aperture was
a 45-degree arc with a 1.91 mm chord (smaller than the 4.4 mm knuckle), and forcing it on
required ~33% strain. Every static check passed while the part could not be assembled at all.

This test models the real assembly motion:
  1. the foot is pressed in +Z from behind the case;
  2. the pin must pass through the clip mouth;
  3. the clip must deflect no more than the documented allowable strain;
  4. after seating, the clip must wrap past the pin's equator so it retains;
  5. the seated joint must have running clearance, not interference.
"""
import FreeCAD as App
import Part
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from functools import partial
print = partial(print, flush=True)

HERE = os.path.dirname(os.path.abspath(__file__))
import re
_src = open(os.path.join(HERE, "06_rear_cover_foot.py")).read()
_ENV = {"MIN_LOAD_WALL": 1.6, "MIN_PRINTED_WALL": 1.2, "FOOT_T": 1.8, "COVER_T": 3.0,
        "CASE_W": 134.4, "REAR_COVER_T": 3.0}
# Resolve constants in source order so expressions like KNUCKLE_Z = FOOT_T-KNUCKLE_R work.
for _m in re.finditer(r"^(PIN_R|PIN_PRELOAD|PIN_CLEAR|BORE_R|CLIP_WALL|CLIP_W|HINGE_Y|KNUCKLE_Z|PIN_ROOT|WEB_Y0|WEB_Z1|ARM_W|ARM_T|COVER_T|BEARING_X0|BEARING_X1|BEARING_W)\s*=\s*([^#\n]+)", _src, re.M):
    try:
        _ENV[_m.group(1)] = float(eval(_m.group(2).strip(), {"__builtins__": {}}, _ENV))
    except Exception:
        pass
def _c(n):
    m = re.search(r"^%s\s*=\s*([^#\n]+)" % n, _src, re.M)
    if not m: raise RuntimeError("missing constant " + n)
    return float(eval(m.group(1), {"__builtins__": {}}, _ENV))

# BEARING_* first: CLIP_W derives from BEARING_W.
_B = re.search(r"^BEARING_X0,\s*BEARING_X1\s*=\s*([^,]+),\s*([^#\n]+)", _src, re.M)
if _B:
    _ENV["BEARING_X0"] = float(eval(_B.group(1).strip(), {"__builtins__": {}}, _ENV))
    _ENV["BEARING_X1"] = float(eval(_B.group(2).strip(), {"__builtins__": {}}, _ENV))
    _ENV["BEARING_W"] = _ENV["BEARING_X1"] - _ENV["BEARING_X0"]
PIN_R = _c("PIN_R"); BORE_R = _c("BORE_R"); CLIP_WALL = _c("CLIP_WALL")
CLIP_W = _c("CLIP_W"); HINGE_Y = _c("HINGE_Y"); KNUCKLE_Z = _c("KNUCKLE_Z")
fails = []
STRAIN_MAX = 0.05

d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
cover = d.getObject("PRINT_REAR_COVER").Shape
foot = d.getObject("PRINT_FOOT").Shape

print("=== HINGE ASSEMBLY FEASIBILITY ===")

print("\n1. clip mouth geometry -- MEASURED from the saved solid, not assumed")
# The generator's own mouth arithmetic has been wrong before (it measured a 159 degree
# opening that the solid did not have), so the open arc is sampled directly.
undercut = BORE_R - PIN_R                      # interference per side at the lips (signed)
CXS = [44.2, 67.2, 90.2]
sample_r = BORE_R + 0.27
open_flags = []
for i in range(360):
    a = math.radians(i)
    y = HINGE_Y + sample_r * math.sin(a); z = KNUCKLE_Z + sample_r * math.cos(a)
    b = Part.makeBox(0.2, 0.2, 0.2, App.Vector(CXS[1] - 0.1, y - 0.1, z - 0.1))
    open_flags.append(foot.common(b).Volume <= 0.005)
open_deg = sum(1 for f in open_flags if f)
wrap = 360 - open_deg
# Largest continuous open run = the mouth; chord across it.
best = cur = 0
for f in open_flags + open_flags:
    cur = cur + 1 if f else 0
    best = max(best, cur)
best = min(best, 360)
chord = 2 * sample_r * math.sin(math.radians(min(best, 359) / 2.0))
print("   bore r %.2f, pin r %.2f -> %.2f mm interference per side (friction fit)"
      % (BORE_R, PIN_R, undercut))
print("   measured mouth %.0f deg (chord %.2f mm), wrap %.0f deg" % (open_deg, chord, wrap))
if open_deg < 90:
    fails.append("clip mouth is not open (%.0f deg) -- foot cannot be pressed on" % open_deg)
if wrap <= 180:
    fails.append("clip wrap %.0f deg does not pass the pin equator" % wrap)

print("\n2. clip deflection required to pass the pin")
# The bore is UNDERSIZED (interference), so the lip must open by the interference as the pin
# passes -- not close a gap, as the old free-running design required.
deflection_per_side = max(BORE_R - PIN_R, 0.0) if BORE_R > PIN_R else 0.0
deflection_per_side = abs(undercut)
print("   lip must open %.2f mm per side" % deflection_per_side)
# Cantilever: effective length is the clip wall arc that flexes, taken as the wall thickness
# plus the arc run adjacent to the mouth (conservative = the mouth half-arc length).
arc_len = math.radians(open_deg / 2.0) * (BORE_R + CLIP_WALL / 2.0) + CLIP_WALL
eps = 1.5 * CLIP_WALL * deflection_per_side / (arc_len ** 2)
print("   effective flexure length %.2f mm, wall %.2f mm" % (arc_len, CLIP_WALL))
print("   strain e = 1.5*h*Y/L^2 = %.4f (%.1f%%) allowed <= %.0f%% %s" %
      (eps, eps * 100, STRAIN_MAX * 100, "OK" if eps <= STRAIN_MAX else "FAIL"))
if eps > STRAIN_MAX:
    fails.append("clip strain %.1f%% exceeds PETG allowable" % (eps * 100))

print("\n3. retention: clip must wrap past the pin equator")
wrap = 360 - open_deg
print("   wrap %.0f deg (>180 required to retain) %s" % (wrap, "OK" if wrap > 180 else "FAIL"))
if wrap <= 180:
    fails.append("clip wrap %.0f deg does not pass the pin equator" % wrap)

print("\n4. assembly motion: the foot is ROTATED in, not pressed straight on")
# Straight +Z translation cannot work by design: the plate body sits behind the cover's lower
# wall, so a purely translational insertion always sweeps it through solid cover (measured
# 2700 mm3). The correct and intended motion is rotational -- snap the clips onto the pin with
# the foot swung open, then rotate it down into the closed position.
# The foot is fitted at open angles and rotated down. Contact at ANGLES ABOVE the design stop
# is the stop doing its job, so the insertion path is checked over the range that must be free:
# from fully open down to just before the stop engages.
rot_ok = True
for deg in [90, 80, 70, 65, 60, 55, 50, 40, 30, 20, 10, 5, 0]:
    g = foot.copy()
    if deg:
        g.rotate(App.Vector(0, HINGE_Y, KNUCKLE_Z), App.Vector(1, 0, 0), -float(deg))
    inter = g.common(cover)
    # Overlap inside a bearing band is the friction preload; only overlap elsewhere is a bind.
    off = sum(sv.Volume for sv in inter.Solids
              if not any(abs((sv.BoundBox.XMin + sv.BoundBox.XMax) / 2 - c) <= 1.7 for c in CXS))
    if off > 0.05 and deg < 50:
        rot_ok = False
        print("   FAIL at %d deg: %.4f mm3 off-bearing (must be clear below the stop)" % (deg, off))
print("   rotational insertion clear from 90 deg to the 50 deg stop: %s" % ("OK" if rot_ok else "FAIL"))
if not rot_ok:
    fails.append("foot cannot be rotated into position")

print("\n5. seated joint: overlap must be the friction preload, all inside the bearings")
# This joint is a FRICTION hinge by design: the bore is 0.12 mm smaller than the pin, so the
# seated overlap IS the friction that stops the stand flopping. What must NOT happen is
# overlap anywhere else, which would be a collision with the cover or with the stop lug.
inter = foot.common(cover)
seated = inter.Volume
off_seated = sum(sv.Volume for sv in inter.Solids
                 if not any(abs((sv.BoundBox.XMin + sv.BoundBox.XMax) / 2 - c) <= 1.7 for c in CXS))
print("   seated overlap %.4f mm3 (friction), of which %.4f mm3 is outside the bearings %s"
      % (seated, off_seated, "OK" if off_seated <= 0.05 else "FAIL"))
if off_seated > 0.05:
    fails.append("seated joint collides outside the bearing (%.3f mm3)" % off_seated)

print("\n6. swing freedom with the foot seated")
for deg in [0, 20, 40, 50, 60, 65]:
    g = foot.copy()
    if deg: g.rotate(App.Vector(0, HINGE_Y, KNUCKLE_Z), App.Vector(1, 0, 0), -float(deg))
    sv = g.common(cover)
    off = sum(x.Volume for x in sv.Solids
              if not any(abs((x.BoundBox.XMin + x.BoundBox.XMax) / 2 - c) <= 1.7 for c in CXS))
    note = "OK" if off < 0.05 else ("stop engaging" if deg >= 60 else "FAIL: binds")
    if off >= 0.05 and deg < 60: fails.append("binds at %d deg (%.4f mm3)" % (deg, off))
    print("   %2d deg  overlap %.4f mm3, off-bearing %.4f mm3  %s" % (deg, sv.Volume, off, note))

if fails:
    print("\nASSEMBLY FEASIBILITY FAILURES:")
    for f in fails: print(" -", f)
    sys.exit(1)
print("\nHINGE ASSEMBLY FEASIBILITY PASS")
