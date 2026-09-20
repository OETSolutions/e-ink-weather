"""Design the stop as a deep buttress and the gusset with fillets.

Two changes the user asked for:
  1. "why don't you have the stop extending all the way down below, there's nothing under it and
     it will still be very weak cantilevered out like that" -> replace the floating ramp + thin
     riser with a solid buttress that runs from the bearing face down to the cover's lowest
     point (z -7.82) and back to the cover's wall, so the load path is short and direct.
  2. "Both the stops and the gussets you added should have fillets in the corners" -> fillet the
     concave corners at the roots, where a cantilever's peak stress sits.

This probe measures, for each candidate: weld volume, foot interference through the FULL swing,
and whether the fillet boolean succeeds.
"""
import os, sys, math
import FreeCAD as App, Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib
m = importlib.import_module("06_rear_cover_foot")

src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = src.getObject("CASE_SHELL").Shape
d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
foot = d.getObject("PRINT_FOOT").Shape

m.STOP_ENABLE = False
cover_ns = m.build_cover(outer, m.build_foot())
m.STOP_ENABLE = True

ca, sa = math.cos(math.radians(m.SWING_DEG)), math.sin(math.radians(m.SWING_DEG))
py, pz = m.STOP_RAMP_P
ey, ez = py + m.STOP_RAMP_LEN * ca, pz - m.STOP_RAMP_LEN * sa

Y_BACK = 4.00
Z_BOT = -7.82

def profile(quad, x0, x1, fillet=0.0, fillet_idx=()):
    pts = [App.Vector(0, y, z) for (y, z) in quad]
    pts.append(pts[0])
    w = Part.makePolygon(pts)
    if fillet > 0.0 and fillet_idx:
        w = w.makeFillet(fillet, [w.Vertexes[i] for i in fillet_idx])
    return Part.Face(w).extrude(App.Vector(x1 - x0, 0, 0)).translate(App.Vector(x0, 0, 0))

def build(quad, fillet=0.0, fillet_idx=()):
    s = None
    for x0, x1 in m.STOP_SPAN_X:
        p = profile(quad, x0, x1, fillet, fillet_idx)
        s = p if s is None else s.fuse(p)
    return s

def measure(name, s):
    try:
        weld = s.common(cover_ns).Volume
    except Exception as e:
        print("   %-24s weld FAILED: %s" % (name, e)); return
    worst_sw, worst_ang = 0.0, 0
    for deg in range(0, 66, 5):
        g = foot.copy()
        if deg:
            g.rotate(App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0), -float(deg))
        ov = s.common(g).Volume
        if ov > worst_sw:
            worst_sw, worst_ang = ov, deg
    print("   %-24s weld %8.3f mm3   solids %d   worst foot interference %8.4f @ %d deg"
          % (name, weld, len(s.Solids), worst_sw, worst_ang))

# candidates -----------------------------------------------------------------------------
# A: the current floating ramp (control)
A = [(py, pz), (ey, ez), (ey - m.STOP_RAMP_T * sa, ez - m.STOP_RAMP_T * ca),
     (py - m.STOP_RAMP_T * sa, pz - m.STOP_RAMP_T * ca)]
# B: buttress all the way down, back to the wall
B = [(py, pz), (ey, ez), (ey, Z_BOT), (Y_BACK, Z_BOT), (Y_BACK, 0.00)]
# C: B with fillets at the two bottom corners and at the root
C = B
# D: deeper back face (more overlap with the wall), still to Z_BOT
D = [(py, pz), (ey, ez), (ey, Z_BOT), (3.00, Z_BOT), (3.00, 0.00)]

print("=== stop candidates (bearing face %0.2f,%0.2f -> %0.2f,%0.2f, unchanged) ==="
      % (py, pz, ey, ez))
measure("A current floating ramp", build(A))
measure("B buttress to wall", build(B))
measure("D buttress, back at y3", build(D))

print("\n=== fillet feasibility on B ===")
for R in (1.0, 1.5, 2.0, 2.5):
    for idx, lab in (((2, 3), "bottom corners"), ((2, 3, 4), "bottom + root")):
        try:
            s = build(B, fillet=R, fillet_idx=idx)
            v = s.common(cover_ns).Volume
            print("   R %.1f %-16s : OK  weld %.3f  solids %d  valid %s"
                  % (R, lab, v, len(s.Solids), s.isValid()))
        except Exception as e:
            print("   R %.1f %-16s : FAILED (%s)" % (R, lab, str(e)[:60]))

print("\n=== does the deep buttress foul the FOOT's own swing? (B, no fillet) ===")
sb = build(B)
for deg in (0, 20, 40, 55, 60, 65, 70, 80, 90):
    g = foot.copy()
    if deg:
        g.rotate(App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0), -float(deg))
    print("   %3d deg : buttress x foot %8.4f mm3" % (deg, sb.common(g).Volume))

print("\n=== and does it stay clear of the assembled chassis? ===")
asm = App.openDocument(os.path.join(HERE, "EInk_Weather_Display_Assembly.FCStd"))
for o in asm.Objects:
    if o.Name in ("PRINT_REAR_COVER", "PRINT_FOOT"):
        continue
    try:
        if o.Shape.Volume > 0:
            print("   %-28s buttress x it = %.4f mm3" % (o.Name, sb.common(o.Shape).Volume))
    except Exception:
        pass
