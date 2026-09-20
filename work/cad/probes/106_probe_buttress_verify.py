"""Verify the buttress + fillets: welds, fillet presence, stop engagement, swing clearance."""
import os, sys, math
import FreeCAD as App, Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib
m = importlib.import_module("06_rear_cover_foot")

d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
cover = d.getObject("PRINT_REAR_COVER").Shape
foot = d.getObject("PRINT_FOOT").Shape
src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = src.getObject("CASE_SHELL").Shape
m.STOP_ENABLE = False
cover_ns = m.build_cover(outer, m.build_foot())
m.STOP_ENABLE = True

FAIL = []

print("=" * 74)
print("1. STOP: buttress weld and fillet presence")
print("=" * 74)
# rebuild the stop exactly as the generator now does
ca, sa = math.cos(math.radians(m.SWING_DEG)), math.sin(math.radians(m.SWING_DEG))
py, pz = m.STOP_RAMP_P
ey, ez = py + m.STOP_RAMP_LEN * ca, pz - m.STOP_RAMP_LEN * sa
quad = [(py, pz), (ey, ez), (ey, m.STOP_Z_BOTTOM), (m.STOP_BACK_Y0, m.STOP_Z_BOTTOM),
        (m.STOP_BACK_Y0, 0.0)]
CONC = {(round(ey, 3), round(ez, 3)), (round(ey, 3), round(m.STOP_Z_BOTTOM, 3)),
        (round(m.STOP_BACK_Y0, 3), round(m.STOP_Z_BOTTOM, 3))}
stop = None
for x0, x1 in m.STOP_SPAN_X:
    pts = [App.Vector(0, y, z) for y, z in quad] + [App.Vector(0, quad[0][0], quad[0][1])]
    pr = Part.Face(Part.makePolygon(pts)).extrude(App.Vector(x1 - x0, 0, 0))
    es = []
    for e in pr.Edges:
        eb = e.BoundBox
        if eb.XMax - eb.XMin < 1.0 - 1e-9:
            continue
        if (round(eb.YMin, 3), round(eb.ZMin, 3)) in CONC or \
           (round(eb.YMax, 3), round(eb.ZMax, 3)) in CONC:
            es.append(e)
    if es:
        pr = pr.makeFillet(m.STOP_FILLET_R, es)
    pr.translate(App.Vector(x0, 0, 0))
    stop = pr if stop is None else stop.fuse(pr)

print("   stop solid %.2f mm3, %d solid(s), valid %s" % (stop.Volume, len(stop.Solids), stop.isValid()))
w = stop.common(cover_ns)
print("   weld %.2f mm3 across %d solid(s)" % (w.Volume, len(w.Solids)))
for s in sorted(w.Solids, key=lambda q: q.BoundBox.XMin):
    b = s.BoundBox
    print("      %7.2f mm3  Y %.2f..%.2f  Z %.2f..%.2f" % (s.Volume, b.YMin, b.YMax, b.ZMin, b.ZMax))
if w.Volume < 60:
    FAIL.append("stop weld small: %.2f" % w.Volume)
# the buttress must reach the cover's lowest point
print("   buttress Zmin %.2f  vs cover Zmin %.2f" % (stop.BoundBox.ZMin, cover.BoundBox.ZMin))
if abs(stop.BoundBox.ZMin - cover.BoundBox.ZMin) > 0.01:
    FAIL.append("buttress does not reach the cover's lowest point")

# fillet presence: the sharp profile had exactly 3 along-X concave edges with radius 0.
# After filleting, those edges are replaced by arc faces. Detect by counting cylindrical faces
# in the Y-Z plane inside the fillet band.
print("\n   fillet check: faces of cylindrical type on the stop")
# rebuild WITHOUT fillet and compare face counts
stop_sharp = None
for x0, x1 in m.STOP_SPAN_X:
    pts = [App.Vector(0, y, z) for y, z in quad] + [App.Vector(0, quad[0][0], quad[0][1])]
    pr = Part.Face(Part.makePolygon(pts)).extrude(App.Vector(x1 - x0, 0, 0))
    pr.translate(App.Vector(x0, 0, 0))
    stop_sharp = pr if stop_sharp is None else stop_sharp.fuse(pr)
def ncyl(s):
    return sum(1 for f in s.Faces if f.Surface.__class__.__name__ == "Cylinder")
print("   sharp stop: %d faces, %d cylindrical" % (len(stop_sharp.Faces), ncyl(stop_sharp)))
print("   filleted  : %d faces, %d cylindrical" % (len(stop.Faces), ncyl(stop)))
if ncyl(stop) <= ncyl(stop_sharp):
    FAIL.append("stop fillet not present")

print("\n" + "=" * 74)
print("2. STOP: engagement and swing (must be unchanged by the rebuild)")
print("=" * 74)
# Measure the STOP's own interference with the foot -- NOT foot/cover, which includes the
# ~7.64 mm3 of intentional clip/pin friction preload at every angle and swamps the signal.
eng = {}
for deg in (63.0, 64.0, 64.5, 65.0, 65.5, 66.0, 70.0):
    g = foot.copy()
    g.rotate(App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0), -float(deg))
    eng[deg] = stop.common(g).Volume
    print("   %5.1f deg : stop x foot %8.4f mm3" % (deg, eng[deg]))
if eng[64.5] > 0.05:
    FAIL.append("stop engages below 65 deg (%.4f at 64.5)" % eng[64.5])
if eng[65.5] < 0.10:
    FAIL.append("stop does not engage above 65 deg (%.4f at 65.5)" % eng[65.5])

print("\n" + "=" * 74)
print("3. FOOT: clip gusset weld with fillets, and clearance")
print("=" * 74)
x0c = m.ARM_CENTERS[1] - m.CLIP_W / 2
plate = m.rprism(m.FOOT_W, m.FOOT_H, 4.0, m.FOOT_X, m.FOOT_Y, 0.0, m.FOOT_T)
ring = Part.makeCylinder(m.BORE_R + m.CLIP_WALL, m.CLIP_W,
                         App.Vector(x0c, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0)).cut(
       Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.2,
                         App.Vector(x0c - 0.1, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0)))
gus = Part.makeBox(m.CLIP_W, m.CLIP_GUSSET_Y1 - m.CLIP_GUSSET_Y0,
                   m.CLIP_GUSSET_Z1 - m.CLIP_GUSSET_Z0,
                   App.Vector(x0c, m.CLIP_GUSSET_Y0, m.CLIP_GUSSET_Z0))
_be = [e for e in gus.Edges
       if e.BoundBox.XMax - e.BoundBox.XMin > m.CLIP_W - 0.01
       and abs((e.BoundBox.YMin + e.BoundBox.YMax) / 2 - m.CLIP_GUSSET_Y0) < 0.02
       and abs((e.BoundBox.ZMin + e.BoundBox.ZMax) / 2 - m.CLIP_GUSSET_Z0) < 0.02
       or e.BoundBox.XMax - e.BoundBox.XMin > m.CLIP_W - 0.01
       and abs((e.BoundBox.YMin + e.BoundBox.YMax) / 2 - m.CLIP_GUSSET_Y1) < 0.02
       and abs((e.BoundBox.ZMin + e.BoundBox.ZMax) / 2 - m.CLIP_GUSSET_Z0) < 0.02]
gusf = gus.makeFillet(m.CLIP_GUSSET_FILLET_R, _be) if _be else gus
gusf = gusf.cut(Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.4,
                                  App.Vector(x0c - 0.2, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0)))
wr = gusf.common(ring).Volume
wp = gusf.common(plate).Volume
print("   gusset x ring %.3f + x plate %.3f = %.3f mm3 per clip (x3 = %.2f)"
      % (wr, wp, wr + wp, 3 * (wr + wp)))
print("   gusset faces %d, cylindrical %d (fillet present if >= 2)"
      % (len(gusf.Faces), ncyl(gusf)))
if ncyl(gusf) < 2:
    FAIL.append("gusset fillet not present")
if wr + wp < 20:
    FAIL.append("gusset weld small: %.2f" % (wr + wp))

print("\n   gusset clearance through the swing")
worst = 0.0
for deg in range(0, 121, 5):
    gg = gusf.copy()
    if deg:
        gg.rotate(App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z), App.Vector(1, 0, 0), -float(deg))
    gg.translate(App.Vector(x0c - (x0c), 0, 0))
    ov = gg.translate(App.Vector(m.ARM_CENTERS[1] - m.ARM_CENTERS[1], 0, 0)).common(cover).Volume
    worst = max(worst, ov)
print("   worst gusset/cover overlap over 0..120 deg: %.4f mm3" % worst)
if worst > 1e-6:
    FAIL.append("gusset collides: %.4f" % worst)

print("\n" + "=" * 74)
if FAIL:
    print("FAILURES:")
    for f in FAIL:
        print("   - " + f)
else:
    print("BUTTRESS + FILLETS VERIFIED")
