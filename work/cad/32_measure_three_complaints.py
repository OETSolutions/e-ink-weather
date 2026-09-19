"""Measure the three current complaints.

1. keyholes: orientation, symmetry about the case centre-line, and material availability
2. hinge: does the detent actually align at the closed angle, and is the interference real
3. hatch: does the flange overlap the cover, and is the screw reachable from OUTSIDE
"""
import FreeCAD as App, Part, os, math
from functools import partial
print = partial(print, flush=True)
HERE = os.path.dirname(os.path.abspath(__file__))
d = App.openDocument(os.path.join(HERE, 'rear_cover_foot.FCStd'))
cover = d.getObject('PRINT_REAR_COVER').Shape
foot = d.getObject('PRINT_FOOT').Shape
hatch = d.getObject('PRINT_SERVICE_HATCH').Shape

CASE_W = 134.4
print("=== 1. KEYHOLE ORIENTATION AND SYMMETRY ===")
# Find where the keyhole features are in the cover.
print("  searching the cover rear face (z 0..0.6) for keyhole-shaped voids:")
col = []
for x in range(0, 135, 1):
    for y in range(40, 75, 1):
        if cover.common(Part.makeBox(1.0,1.0,0.6,App.Vector(float(x),float(y),0.0))).Volume < 0.02:
            col.append((x,y))
if col:
    xs = [c[0] for c in col]; ys=[c[1] for c in col]
    print("    void spans x %.1f..%.1f  y %.1f..%.1f  (%d samples)" % (min(xs),max(xs),min(ys),max(ys),len(col)))
    # cluster by x
    xs_sorted = sorted(set(xs)); groups=[]; cur=[xs_sorted[0]]
    for v in xs_sorted[1:]:
        if v-cur[-1] <= 2: cur.append(v)
        else: groups.append(cur); cur=[v]
    groups.append(cur)
    print("    void x-clusters:", [(g[0],g[-1]) for g in groups])
    for g in groups:
        gy=[c[1] for c in col if c[0] in g]
        print("      cluster x %d..%d : y %d..%d  (height %d)" % (g[0],g[-1],min(gy),max(gy),max(gy)-min(gy)))
print("  case centre-line x = %.1f" % (CASE_W/2))

print("\n=== 2. HINGE: detent alignment and interference ===")
PIN_R=2.20; BORE_R=2.08; HINGE_Y=5.0; KZ=3.0-(2.08+1.6)
print("  pin r %.2f, clip bore r %.2f -> interference %.2f mm/side (real if >0)" % (PIN_R,BORE_R,PIN_R-BORE_R))
print("  preload overlap measured: %.2f mm3" % cover.common(foot).Volume)
# Where is the pin's groove, angularly, and where is the clip's rib?
groove=[]; rib=[]
for a in range(0,360,10):
    r=math.radians(a)
    pg=Part.makeBox(1.2,0.5,0.5,App.Vector(43.2, HINGE_Y+2.0*math.cos(r)-0.25, KZ+2.0*math.sin(r)-0.25))
    if cover.common(pg).Volume < 0.05: groove.append(a)
    pf=Part.makeBox(1.2,0.5,0.5,App.Vector(43.2, HINGE_Y+2.6*math.cos(r)-0.25, KZ+2.6*math.sin(r)-0.25))
    if foot.common(pf).Volume > 0.05: rib.append(a)
print("  pin groove angles (void at r2.0): %s" % groove)
print("  clip rib angles (material at r2.6): %s" % rib)
print("  -> they must OVERLAP at the closed angle for the detent to do anything")

print("\n=== 3. HATCH: flange overlap and outside screw access ===")
hb = hatch.BoundBox
print("  hatch bbox X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f" % (hb.XMin,hb.XMax,hb.YMin,hb.YMax,hb.ZMin,hb.ZMax))
bay = (22.5,79.2,68.0,92.0)
print("  bay opening X %.1f..%.1f Y %.1f..%.1f" % bay)
print("  flange X size %.1f vs bay X %.1f -> overlap each side %.1f mm" %
      (hb.XLength, bay[1]-bay[0], (hb.XLength-(bay[1]-bay[0]))/2))
print("  Z: cover exterior face is z=0; hatch extends to z=%.1f (negative = outside)" % hb.ZMin)
print("  screw hole: is there a through-hole in the hatch for a screw from OUTSIDE?")
for z in (-1.0,-0.5,0.0,0.5,1.0):
    v = hatch.common(Part.makeCylinder(1.3,0.2,App.Vector(50.85,62.0,z))).Volume
    print("    z %5.1f material in screw column %.4f" % (z,v))
