"""Fillet the clip gussets: which construction actually builds?

User: "Both the stops and the gussets you added should have fillets in the corners."

A gusset fails at its CONCAVE root -- here, where the gusset's material meets the ring's outer
cylinder. Test candidates for both buildability and weld volume.
"""
import os, sys, math
import FreeCAD as App, Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib
m = importlib.import_module("06_rear_cover_foot")

CX = m.ARM_CENTERS[1]
HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
OUT_R = m.BORE_R + m.CLIP_WALL
plate = m.rprism(m.FOOT_W, m.FOOT_H, 4.0, m.FOOT_X, m.FOOT_Y, 0.0, m.FOOT_T)

def bore_cut(s):
    return s.cut(Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.4,
                                   App.Vector(CX - m.CLIP_W / 2 - 0.2, HY, HZ), App.Vector(1, 0, 0)))

ring = Part.makeCylinder(OUT_R, m.CLIP_W, App.Vector(CX - m.CLIP_W / 2, HY, HZ),
                         App.Vector(1, 0, 0)).cut(
       Part.makeCylinder(m.BORE_R, m.CLIP_W + 0.2,
                         App.Vector(CX - m.CLIP_W / 2 - 0.1, HY, HZ), App.Vector(1, 0, 0)))

# where the ring's outer circle crosses the gusset's bottom plane Z=-2.80
ZB = m.CLIP_GUSSET_Z0
dy = math.sqrt(OUT_R**2 - (ZB - HZ)**2)
Y_ON_RING = HY + dy                      # 8.008
print("ring outer crosses Z %.2f at Y %.3f ; gusset Y0 %.2f Y1 %.2f"
      % (ZB, Y_ON_RING, m.CLIP_GUSSET_Y0, m.CLIP_GUSSET_Y1))

def weld(s):
    return s.common(ring.fuse(plate)).Volume

def report(name, g):
    try:
        valid = g.isValid()
        print("   %-34s weld %7.3f  valid %s  solids %d"
              % (name, weld(g), valid, len(g.Solids)))
    except Exception as e:
        print("   %-34s FAILED %s" % (name, str(e)[:50]))

print("\n=== gusset constructions ===")

# A: current plain box
A = Part.makeBox(m.CLIP_W, m.CLIP_GUSSET_Y1 - m.CLIP_GUSSET_Y0,
                 m.CLIP_GUSSET_Z1 - m.CLIP_GUSSET_Z0,
                 App.Vector(CX - m.CLIP_W / 2, m.CLIP_GUSSET_Y0, ZB))
report("A plain box (current)", bore_cut(A))

# B: box, fillet its own concave-adjacent bottom edge before fusing is meaningless; instead
#    fuse into the foot and fillet the fused result's gusset-root edges.
footB = plate.fuse(ring).fuse(bore_cut(A))
# candidate edges: near the root line Y~8.0, Z~-2.8, within the clip's X band
root_edges = []
for e in footB.Edges:
    bb = e.BoundBox
    if (abs(bb.XMin - (CX - m.CLIP_W / 2)) < 0.05 or abs(bb.XMax - (CX + m.CLIP_W / 2)) < 0.05) \
       and bb.YMax < m.CLIP_GUSSET_Y1 + 0.5 and bb.ZMax < ZB + 0.5:
        root_edges.append(e)
print("   root edges found on the fused foot: %d" % len(root_edges))
for R in (0.6, 0.8, 1.0, 1.2):
    try:
        f = footB.makeFillet(R, root_edges)
        print("   %-34s weld %7.3f  valid %s  solids %d"
              % ("B fused foot + fillet R%.1f" % R, weld(f), f.isValid(), len(f.Solids)))
    except Exception as e:
        print("   %-34s FAILED %s" % ("B fused foot + fillet R%.1f" % R, str(e)[:45]))

# C: arc-profile gusset -- its -Y boundary FOLLOWS the ring's outer cylinder, so the root is a
#    tangent blend rather than a sharp concave corner.
prof = [(HY + math.sqrt(OUT_R**2 - (ZB - HZ)**2), ZB)]
# walk the circle from ZB up to Z=0
N = 24
a0 = math.atan2(ZB - HZ, Y_ON_RING - HY)
a1 = math.atan2(0.0 - HZ, math.sqrt(OUT_R**2 - (0.0 - HZ)**2))
for i in range(1, N + 1):
    a = a0 + (a1 - a0) * i / N
    prof.append((HY + OUT_R * math.cos(a), HZ + OUT_R * math.sin(a)))
prof += [(m.CLIP_GUSSET_Y1, 0.0), (m.CLIP_GUSSET_Y1, ZB)]
pts = [App.Vector(0, y, z) for y, z in prof] + [App.Vector(0, prof[0][0], prof[0][1])]
C = Part.Face(Part.makePolygon(pts)).extrude(App.Vector(m.CLIP_W, 0, 0)).translate(
        App.Vector(CX - m.CLIP_W / 2, 0, 0))
report("C arc-profile gusset", bore_cut(C))

# D: arc profile with a fillet where the bottom meets the arc
try:
    edges = [e for e in C.Edges
             if abs(e.BoundBox.XMax - e.BoundBox.XMin) > 1.0
             and abs(e.BoundBox.ZMin - ZB) < 0.02 and abs(e.BoundBox.ZMax - ZB) < 0.02
             and e.BoundBox.YMin < Y_ON_RING + 0.05]
    D = C.makeFillet(0.8, edges)
    report("D arc + fillet R0.8", bore_cut(D))
except Exception as e:
    print("   %-34s FAILED %s" % ("D arc + fillet R0.8", str(e)[:45]))
