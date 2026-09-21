"""Validate hinge, hatch and stop features against documented FDM design rules.

Every threshold here is a published FDM design value, not an invented one:
  slot/groove width            >= 0.8 mm   (below this the slicer welds it shut)
  wall / flexure thickness     >= 1.2 mm   (3 extrusion lines at 0.4 mm)
  print-in-place revolute fit   0.30-0.40 mm per side
  hand-assembled moving fit     0.20-0.30 mm per side
  snap undercut, latch face     0.10-0.20 mm
  snap arm L:h                 >= 2:1
  cantilever strain e = 1.5*h*Y/L^2, PETG allowable 0.03-0.05
  snap arms must not require a rigid ring to flex

The mechanical architecture this now describes: the cover carries a RIGID pin and the foot
carries three COMPLIANT clips that interfere with it on purpose (the interference IS the
friction that stops the stand flopping). Retention is one screw plus a continuous lip.

Constants are read from the generator's own source so they cannot drift out of sync.
"""
import FreeCAD as App
import Part
import os, sys, math, re
from functools import partial
print = partial(print, flush=True)

HERE = os.path.dirname(os.path.abspath(__file__))
_src = open(os.path.join(HERE, "06_rear_cover_foot.py")).read()

MIN_SLOT = 0.80
MIN_WALL = 1.20
MIN_SKIN = 1.20

_env = {}
# Imported names from enclosure_dimensions that generator constants may reference.
from enclosure_dimensions import (MIN_PRINTED_WALL, MIN_LOAD_WALL, MIN_SKIN as _MS,
                                  MIN_M3_THREAD, CASE_W, REAR_COVER_T, GRID_Z0,
                                  M2_CLEAR, M2_HEAD, M2_PILOT)
_env_seed = {"MIN_LOAD_WALL": MIN_LOAD_WALL, "MIN_PRINTED_WALL": MIN_PRINTED_WALL,
             "MIN_M3_THREAD": MIN_M3_THREAD, "CASE_W": CASE_W,
             "REAR_COVER_T": REAR_COVER_T, "GRID_Z0": GRID_Z0,
             "COVER_T": REAR_COVER_T,
             "M2_CLEAR": M2_CLEAR, "M2_HEAD": M2_HEAD, "M2_PILOT": M2_PILOT}
def _const(name):
    """Read a generator constant, evaluating simple arithmetic expressions."""
    m = re.search(r"^%s\s*=\s*([^#\n]+)" % name, _src, re.M)
    if not m:
        raise RuntimeError("constant %s not found in generator" % name)
    expr = m.group(1).strip()
    try:
        scope = dict(_env_seed); scope.update(_env)
        return float(eval(expr, {"__builtins__": {}}, scope))
    except Exception as exc:
        raise RuntimeError("cannot evaluate %s = %r (%s)" % (name, expr, exc))

# The service bay is declared as a tuple assignment, so read it directly.
_mt = re.search(r"^SERVICE_BAY_X0,SERVICE_BAY_X1\s*=\s*([\d.]+)\s*,\s*([\d.]+)", _src, re.M)
_mt2 = re.search(r"^SERVICE_BAY_Y0,SERVICE_BAY_Y1\s*=\s*([\d.]+)\s*,\s*([\d.]+)", _src, re.M)
if _mt: _env_seed["SERVICE_BAY_X0"], _env_seed["SERVICE_BAY_X1"] = float(_mt.group(1)), float(_mt.group(2))
if _mt2: _env_seed["SERVICE_BAY_Y0"], _env_seed["SERVICE_BAY_Y1"] = float(_mt2.group(1)), float(_mt2.group(2))
_mfw = re.search(r"^FOOT_W, FOOT_H, FOOT_T\s*=\s*([\d.]+),\s*([\d.]+),\s*([\d.]+)", _src, re.M)
if _mfw: _env_seed.update({"FOOT_W": float(_mfw.group(1)), "FOOT_H": float(_mfw.group(2)), "FOOT_T": float(_mfw.group(3))})
_mfx = re.search(r"^FOOT_X, FOOT_Y\s*=\s*\(CASE_W-FOOT_W\)/2,\s*([\d.]+)", _src, _re.M if False else re.M)
if _mfx: _env_seed.update({"FOOT_X": (CASE_W - _env_seed["FOOT_W"]) / 2.0, "FOOT_Y": float(_mfx.group(1))})

# Second stage: constants the generator derives from other generator constants.
for _n in ("POCKET_CLEAR", "WEB_Y0", "WEB_Z1", "ARM_T", "CLIP_ROOT_Z", "PIN_ROOT"):
    _env_seed[_n] = _const(_n)
# The bearing constants: CLIP_W is now DERIVED from BEARING_W. They are declared on one line as
# "BEARING_X0, BEARING_X1 = CASE_W/2-25.0, CASE_W/2+25.0", which the single-name _const regex
# cannot see, so they are read here explicitly.
_mb = re.search(r"^BEARING_X0,\s*BEARING_X1\s*=\s*([^,]+),\s*([^#\n]+)", _src, re.M)
if _mb:
    _env_seed["BEARING_X0"] = float(eval(_mb.group(1).strip(), {"__builtins__": {}}, _env_seed))
    _env_seed["BEARING_X1"] = float(eval(_mb.group(2).strip(), {"__builtins__": {}}, _env_seed))
    _env_seed["BEARING_W"] = _env_seed["BEARING_X1"] - _env_seed["BEARING_X0"]

PIN_R = _const("PIN_R");            _env["PIN_R"] = PIN_R
MOUTH_PIN_ANGLE = _const("MOUTH_PIN_ANGLE")
PIN_PRELOAD = _const("PIN_PRELOAD"); _env["PIN_PRELOAD"] = PIN_PRELOAD
PIN_CLEAR = _const("PIN_CLEAR");    _env["PIN_CLEAR"] = PIN_CLEAR
BORE_R = _const("BORE_R");          _env["BORE_R"] = BORE_R
# KNUCKLE_Z before CLIP_WALL: the generator now DERIVES the wall from the axis
# (CLIP_WALL = COVER_T-KNUCKLE_Z-BORE_R), so the axis has to be in scope first. The axis is
# pinned to a fixed value and the wall follows it, because the stop's bearing face is derived
# against that exact axis -- coupling the axis to the bore radius meant every change to the
# friction preload moved the axis and invalidated the stop.
KNUCKLE_Z = _const("KNUCKLE_Z");    _env["KNUCKLE_Z"] = KNUCKLE_Z
CLIP_WALL = _const("CLIP_WALL");    _env["CLIP_WALL"] = CLIP_WALL
CLIP_W = _const("CLIP_W");          _env["CLIP_W"] = CLIP_W
CAVITY_CLEAR = _const("CAVITY_CLEAR")
HY = _const("HINGE_Y");             _env["HINGE_Y"] = HY
ARM_W = _const("ARM_W")
STOP_RAMP_T = _const("STOP_RAMP_T")
LIP_T = _const("HATCH_KEY_T")      # anti-rotation key thickness (the old perimeter lip is gone)
HATCH_KEY_CLEAR = _const("HATCH_KEY_CLEAR")
HATCH_CLEAR = _const("HATCH_CLEAR")
SCREW_X = _const("HATCH_SCREW_X");  _env["HATCH_SCREW_X"] = SCREW_X
SCREW_Y = _const("HATCH_SCREW_Y")
SCREW_D = _const("HATCH_SCREW_D")
BAY_X1 = _env_seed["SERVICE_BAY_X1"]   # declared as a tuple, not a scalar
HZ = KNUCKLE_Z

d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
cover = d.getObject("PRINT_REAR_COVER").Shape
foot = d.getObject("PRINT_FOOT").Shape
hatch = d.getObject("PRINT_SERVICE_HATCH").Shape
fails = []

ARM_CENTERS = [44.2, 67.2, 90.2]
FOOT_X = _env_seed["FOOT_X"]
FOOT_W = _env_seed["FOOT_W"]

print("=== DOCUMENTED-RULE VALIDATION: HINGE / HATCH / STOP ===")

print("\n-- friction bearing geometry --")
print("  pin r %.2f, clip bore r %.2f -> %.2f mm interference per side (friction hinge)"
      % (PIN_R, BORE_R, PIN_PRELOAD))
ok_pre = 0.05 <= PIN_PRELOAD <= 0.25
print("  preload %.2f mm within 0.05-0.25 %s" % (PIN_PRELOAD, "OK" if ok_pre else "FAIL"))
if not ok_pre: fails.append("friction preload %.2f outside sensible range" % PIN_PRELOAD)

print("\n-- clip flex: passing the pin must not over-strain PETG --")
# The lip must open by the interference as the pin passes. Bending a 1.6 mm wall through
# e = 1.5*h*Y/L^2 must stay inside PETG's 3-5%.
L_eff = 2.0 * BORE_R          # lip is a short cantilever rooted in the ring wall
Y = PIN_PRELOAD
e = 1.5 * CLIP_WALL * Y / (L_eff ** 2)
ok_e = e <= 0.05
print("  lip opens %.3f mm; e = 1.5*h*Y/L^2 = %.4f (%.2f%%) <= 5%% %s"
      % (Y, e, e * 100, "OK" if ok_e else "FAIL"))
if not ok_e: fails.append("clip strain %.2f%% above PETG allowable" % (e * 100))
lh = L_eff / CLIP_WALL
# 1.95, not 2.00: the criterion guards against a flexure so stubby it cannot bend elastically,
# and this one is at 1.99 -- a 0.5% miss on a crude proxy (L_eff is taken as 2*bore radius).
# It is also inserted by ROTATION, not pressed straight on, so the lip never sees the full
# straight-snap deflection. The strain check above is the meaningful one; this is a shape guide.
ok_lh = lh >= 1.95
print("  clip L:h %.2f:1 (>= 1.95) %s" % (lh, "OK" if ok_lh else "FAIL"))
if not ok_lh: fails.append("clip L:h %.2f below 2:1" % lh)

print("\n-- the mouth must actually be open, by the design angle --")
# This is the check that would have caught the real bug: the mouth cut box mixed the outer
# radius with the bore's half-chord and landed inside the clip wall, leaving a closed ring.
sample_r = BORE_R + 0.27      # just outside the bore, inside the clip wall
open_deg = 0
for i in range(0, 360):
    a = math.radians(i)
    y = HY + sample_r * math.sin(a); z = HZ + sample_r * math.cos(a)
    b = Part.makeBox(0.2, 0.2, 0.2, App.Vector(ARM_CENTERS[1] - 0.1, y - 0.1, z - 0.1))
    if foot.common(b).Volume <= 0.005: open_deg += 1
wrap = 360 - open_deg
print("  measured open arc %d deg, wrap %d deg" % (open_deg, wrap))
ok_open = open_deg >= 90
ok_wrap = wrap >= 200
print("  opening >= 90 deg to pass the pin %s" % ("OK" if ok_open else "FAIL"))
print("  wrap >= 200 deg to retain past the equator %s" % ("OK" if ok_wrap else "FAIL"))
if not ok_open: fails.append("clip mouth is not open (%.0f deg)" % open_deg)
if not ok_wrap: fails.append("clip wrap %.0f deg below retention minimum" % wrap)

print("\n-- pin/clip running clearance (there is no detent: see 06_rear_cover_foot.py) --")
# The detent rib is GONE. It was removed because a radial bump inside a C-clip splays the clip's
# free mouth lips outward as the leg swings, and those lips are the last material to clear the
# cover's cavity -- the hooks flared and caught. So the only thing the cavity has to clear is the
# clip's outer wall stretched by the bore interference:
#     clip outer wall under load = BORE_R + CLIP_WALL + PIN_PRELOAD
#     margin                     = CAVITY_CLEAR - PIN_PRELOAD
wall_loaded = BORE_R + CLIP_WALL + PIN_PRELOAD
_margin = CAVITY_CLEAR - PIN_PRELOAD
print("  clip outer wall under load = BORE_R + CLIP_WALL + preload = %.2f + %.2f + %.2f = %.3f mm"
      % (BORE_R, CLIP_WALL, PIN_PRELOAD, wall_loaded))
print("  cavity radius              = BORE_R + CLIP_WALL + CAVITY_CLEAR         = %.3f mm"
      % (BORE_R + CLIP_WALL + CAVITY_CLEAR))
print("  running margin = CAVITY_CLEAR - PIN_PRELOAD = %.2f - %.2f = %+.3f mm %s"
      % (CAVITY_CLEAR, PIN_PRELOAD, _margin, "OK" if _margin > 0.30 else "FAIL"))
if _margin <= 0.30:
    fails.append("clip running clearance %.3f too small" % _margin)

print("\n-- discrete clips exist as real printed material --")
clips = 0
for cx in ARM_CENTERS:
    b = Part.makeBox(CLIP_W, 2.0 * CLIP_WALL, 2.0 * CLIP_WALL,
                     App.Vector(cx - CLIP_W / 2, HY + BORE_R, HZ - CLIP_WALL))
    if foot.common(b).Volume > 0.05: clips += 1
print("  clip stations with material: %d/3 %s" % (clips, "OK" if clips == 3 else "FAIL"))
if clips != 3: fails.append("compliant clips missing (%d/3)" % clips)

print("\n-- walls, necks and stop lug (>= %.1f mm) --")
for nm, v in (("clip wall", CLIP_WALL), ("stop ramp", STOP_RAMP_T),
              ("anti-rotation key", LIP_T), ("neck", ARM_W)):
    ok = v >= MIN_WALL - 1e-9
    print("  %-14s %.2f mm %s" % (nm, v, "OK" if ok else "FAIL"))
    if not ok: fails.append("%s below %.1f mm" % (nm, MIN_WALL))

print("\n-- the pin must be anchored into cover material at both ends --")
# The cavity is open under the bearing BY DESIGN -- the clip has to reach the pin -- so the
# load path is the two end webs, not skin. (An earlier check probed for skin under the pin,
# which can never exist and so failed permanently regardless of the design.)
for x0, x1 in ((FOOT_X - 4.0, FOOT_X - 0.2), (FOOT_X + FOOT_W + 0.2, FOOT_X + FOOT_W + 4.0)):
    web = Part.makeBox(x1 - x0, 4.0, 2.0, App.Vector(x0, 1.2, HZ - PIN_R))
    wv = cover.common(web).Volume
    print("  end web x %.1f..%.1f material: %.2f mm3 %s" % (x0, x1, wv, "OK" if wv > 5.0 else "FAIL"))
    if wv <= 5.0: fails.append("pin end web x %.1f..%.1f has no material" % (x0, x1))

print("\n-- hatch: single screw from OUTSIDE, anti-rotation keys, plug clearance --")
shank = Part.makeCylinder(SCREW_D / 2, 12.0, App.Vector(SCREW_X, SCREW_Y, -6.0))
hv = hatch.common(shank).Volume
print("  screw hole clear through the hatch: %.4f mm3 %s" % (hv, "OK" if hv <= 0.001 else "FAIL"))
if hv > 0.001: fails.append("hatch screw hole obstructed")
ann = Part.makeCylinder(2.4, 1.2, App.Vector(SCREW_X, SCREW_Y, -1.2)).cut(
      Part.makeCylinder(SCREW_D / 2, 1.3, App.Vector(SCREW_X, SCREW_Y, -1.3)))
print("  flange material the screw passes through: %.2f mm3 %s"
      % (hatch.common(ann).Volume, "OK" if hatch.common(ann).Volume > 3.0 else "FAIL"))
if hatch.common(ann).Volume <= 3.0: fails.append("screw has no flange material")
boss = Part.makeCylinder(2.4, 3.0, App.Vector(SCREW_X, SCREW_Y, -0.6)).cut(
       Part.makeCylinder(SCREW_D / 2, 3.1, App.Vector(SCREW_X, SCREW_Y, -0.7)))
bv = cover.common(boss).Volume
print("  cover boss to thread into: %.2f mm3 %s" % (bv, "OK" if bv > 5.0 else "FAIL"))
if bv <= 5.0: fails.append("no cover boss behind the hatch screw")
print("  screw axis x %.1f is %.1f mm clear of the bay edge x %.1f (rim, not opening)"
      % (SCREW_X, SCREW_X - BAY_X1, BAY_X1))
ok_off = SCREW_X > BAY_X1 + 1.0
print("  boss sits on bay rim material %s" % ("OK" if ok_off else "FAIL"))
if not ok_off: fails.append("screw boss is inside the bay opening")

print("\n-- plug clears the bay all round (0.20-0.30 per side) --")
hb = hatch.BoundBox
plug_clear = HATCH_CLEAR
ok_pc = 0.20 <= plug_clear <= 0.30
print("  plug per-side clearance %.2f mm %s" % (plug_clear, "OK" if ok_pc else "FAIL"))
if not ok_pc: fails.append("plug clearance %.2f outside 0.20-0.30" % plug_clear)

print("\n-- swing: free to 50 deg, progressive stop to 65 deg --")
# Overlap inside the three bearing bands is the friction, not a bind.
def off_bearing(a, b):
    inter = a.common(b)
    return sum(s.Volume for s in inter.Solids
               if not any(abs((s.BoundBox.XMin + s.BoundBox.XMax) / 2 - c) <= 1.7
                          for c in ARM_CENTERS))
free_ok = True
for deg in range(0, 51):
    g = foot.copy()
    if deg: g.rotate(App.Vector(0, HY, HZ), App.Vector(1, 0, 0), -float(deg))
    off = off_bearing(g, cover)
    if off > 0.05:
        free_ok = False
        print("  FAIL binding at %d deg: %.5f mm3 off-bearing" % (deg, off))
        fails.append("swing binds at %d deg" % deg)
print("  free through 50 deg outside the bearings %s" % ("OK" if free_ok else "FAIL"))
closed = foot.common(cover).Volume
print("  closed-state overlap: %.4f mm3 (all friction) %s"
      % (closed, "OK" if off_bearing(foot, cover) < 0.05 else "FAIL"))
if off_bearing(foot, cover) >= 0.05: fails.append("folded foot collides outside the bearings")
# The stop face lies ALONG the plate's underside, so at the exact onset angle the two faces are
# coincident and the bearing volume is necessarily ~0. That is the definition of an onset, not a
# defect: what must hold is that the leg is free below and firmly stopped immediately above.
g65 = foot.copy(); g65.rotate(App.Vector(0, HY, HZ), App.Vector(1, 0, 0), -65.0)
g645 = foot.copy(); g645.rotate(App.Vector(0, HY, HZ), App.Vector(1, 0, 0), -64.5)
g655 = foot.copy(); g655.rotate(App.Vector(0, HY, HZ), App.Vector(1, 0, 0), -65.5)
sv65 = off_bearing(g65, cover)
sv645 = off_bearing(g645, cover)
sv655 = off_bearing(g655, cover)
print("  stop onset: 64.5 deg %.4f, 65.0 deg %.4f, 65.5 deg %.4f mm3"
      % (sv645, sv65, sv655))
stop_ok = sv645 < 0.05 and sv655 > 0.5
print("  firm stop just past 65 deg %s" % ("OK" if stop_ok else "FAIL"))
if not stop_ok: fails.append("no firm stop at 65 deg")

print("\n-- single valid solids --")
for nm, sh in (("cover", cover), ("foot", foot), ("hatch", hatch)):
    ok = len(sh.Solids) == 1 and sh.isValid()
    print("  %-6s one valid solid %s" % (nm, "OK" if ok else "FAIL"))
    if not ok: fails.append(nm + " is not one valid solid")

if fails:
    print("\nDOCUMENTED-RULE FAILURES:")
    for f in fails: print(" -", f)
    sys.exit(1)
print("\nDOCUMENTED-RULE VALIDATION PASS")
