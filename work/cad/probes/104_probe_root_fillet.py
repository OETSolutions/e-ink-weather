"""Give the stop a genuine concave ROOT fillet, and the gusset fillets at its root.

A convex profile has no concave corner to fillet, so the root fillet has to be built into the
profile: run the back face up into the wall, and make the junction a CONCAVE corner.

Profile (closed):
  1 bearing face top   (7.10,-3.56)
  2 bearing face bottom(8.495,-6.551)
  3 bottom outer       (8.495,-7.82)
  4 base inner         (3.20,-7.82)      <- broad base, rests on the bed
  5 root               (4.00,-2.45)      <- CONCAVE: fillet here (the wall-bottom junction)
  6 inside the wall    (4.00, 0.00)

Measure buildability, weld and foot interference.
"""
import os, sys, math
import FreeCAD as App, Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib
m = importlib.import_module("06_rear_cover_foot")

src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = src.getObject("CASE_SHELL").Shape
m.STOP_ENABLE = False
cover_ns = m.build_cover(outer, m.build_foot())
m.STOP_ENABLE = True
d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
foot = d.getObject("PRINT_FOOT").Shape

ca, sa = math.cos(math.radians(m.SWING_DEG)), math.sin(math.radians(m.SWING_DEG))
py, pz = m.STOP_RAMP_P
ey, ez = py + m.STOP_RAMP_LEN * ca, pz - m.STOP_RAMP_LEN * sa

P = [(py, pz), (ey, ez), (ey, -7.82), (3.20, -7.82), (4.00, -2.45), (4.00, 0.00)]
ROOT = (4.00, -2.45)
TOES = [(8.495, -6.551), (8.495, -7.82), (3.20, -7.82)]

def prism(quad, x0, x1):
    pts = [App.Vector(0, y, z) for y, z in quad] + [App.Vector(0, quad[0][0], quad[0][1])]
    return Part.Face(Part.makePolygon(pts)).extrude(App.Vector(x1 - x0, 0, 0))

def sel_edges(pr, corners):
    out = []
    for e in pr.Edges:
        eb = e.BoundBox
        if eb.XMax - eb.XMin < 1.0:      # profile edge, skip
            continue
        y = round((eb.YMin + eb.YMax) / 2, 3); z = round((eb.ZMin + eb.ZMax) / 2, 3)
        for cy, cz in corners:
            if abs(y - cy) < 0.02 and abs(z - cz) < 0.02:
                out.append(e)
    return out

def build(quad, corners, R):
    s = None
    for x0, x1 in m.STOP_SPAN_X:
        pr = prism(quad, x0, x1)
        es = sel_edges(pr, corners)
        if es:
            pr = pr.makeFillet(R, es)
        pr.translate(App.Vector(x0, 0, 0))
        s = pr if s is None else s.fuse(pr)
    return s

def evaluate(tag, s):
    try:
        w = s.common(cover_ns).Volume
    except Exception as e:
        print("   %-30s weld FAILED %s" % (tag, str(e)[:40])); return
    worst, wa = 0.0, 0
    for deg in range(0, 66, 5):
        g = foot.copy()
        if deg:
            g.rotate(App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0), -float(deg))
        ov = s.common(g).Volume
        if ov > worst:
            worst, wa = ov, deg
    print("   %-30s weld %7.2f  valid %-5s solids %d  foot max %7.4f @%d deg"
          % (tag, w, s.isValid(), len(s.Solids), worst, wa))

print("=== root fillet sweep (concave junction at %.2f,%.2f) ===" % ROOT)
for R in (0.6, 0.8, 1.0, 1.2, 1.5, 2.0):
    try:
        s = build(P, [ROOT], R)
    except Exception as e:
        print("   root only R%.1f : FAILED %s" % (R, str(e)[:45])); continue
    evaluate("root only R%.1f" % R, s)

print("\n=== root + toe fillets ===")
for R in (0.6, 0.8, 1.0):
    try:
        s = build(P, [ROOT] + TOES, R)
    except Exception as e:
        print("   root+toes R%.1f : FAILED %s" % (R, str(e)[:45])); continue
    evaluate("root+toes R%.1f" % R, s)

print("\n=== no fillet (control) ===")
evaluate("plain buttress", build(P, [], 0.0))
