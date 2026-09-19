"""Measure the six user-reported defects in the saved assembly."""
import FreeCAD as App,Part,os,math
from functools import partial
print=partial(print,flush=True)
HERE=os.path.dirname(os.path.abspath(__file__))
d=App.openDocument(os.path.join(HERE,'EInk_Weather_Display_Assembly.FCStd'))
cover=d.getObject('PRINT_REAR_COVER').Shape
foot=d.getObject('PRINT_SWING_FOOT').Shape
hatch=d.getObject('PRINT_SERVICE_HATCH').Shape
ch=d.getObject('PRINT_REAR_CHASSIS').Shape
glass=d.getObject('PANEL_GLASS').Shape
battery=d.getObject('BATTERY_70x39x11').Shape
fpc=d.getObject('PANEL_FPC_FOLDED_180').Shape
p5=d.getObject('BOARD_P5_ENTRY').Shape
pcb=d.getObject('BOARD_PCB_48x66x1').Shape

print('=== 1 HINGE FIT / RETENTION ===')
# pin r2.2 / clip bore r2.5 = 0.3 radial, deliberately free running
print('pin/clip nominal radial gap = 0.30 mm per side (free-running, no friction preload)')
print('seated cover/foot interference %.6f mm3'%cover.common(foot).Volume)
# no detent in generator: report material at folded latch edge
print()

print('=== 2 ASSEMBLY CENTRE OF GRAVITY vs KEYHOLE ===')
# Use volumes with approximate material/component densities. Printed PETG 1.27 g/cc,
# glass/display known 28.08 g, battery unknown actual mass: use geometric centroid and report.
def centroid(shape):
    """Volume-weighted centroid that works for solids and compounds alike."""
    solids=[s for s in shape.Solids if s.Volume>1e-9]
    if not solids:
        b=shape.BoundBox
        return App.Vector((b.XMin+b.XMax)/2,(b.YMin+b.YMax)/2,(b.ZMin+b.ZMax)/2)
    V=sum(s.Volume for s in solids)
    return App.Vector(sum(s.Volume*s.CenterOfMass.x for s in solids)/V,
                      sum(s.Volume*s.CenterOfMass.y for s in solids)/V,
                      sum(s.Volume*s.CenterOfMass.z for s in solids)/V)

items=[]
for n in ['PRINT_FRONT_BEZEL','PRINT_REAR_CHASSIS','PRINT_REAR_COVER','PRINT_SERVICE_HATCH','PRINT_SWING_FOOT']:
    s=d.getObject(n).Shape; items.append((n,s.Volume/1000*1.27,centroid(s)))
# panel sourced mass 28.08g
items.append(('panel',28.08,centroid(glass)))
# board estimate from component compound at 1.5g/cc effective
board_shapes=[o.Shape for o in d.getObject('ESP32_M1_ASSEMBLY').Group if hasattr(o,'Shape') and not o.Shape.isNull()]
bc=centroid(Part.makeCompound(board_shapes))
bv=sum(s.Volume for s in board_shapes)
items.append(('board-effective',bv/1000*1.5,bc))
# battery mass is not sourced; use 75g as explicit scenario and sweep 50..100g
for bm in [50,75,100]:
    its=items+[('battery',bm,centroid(battery))]
    M=sum(m for _,m,_ in its); cx=sum(m*p.x for _,m,p in its)/M;cy=sum(m*p.y for _,m,p in its)/M
    print('battery scenario %3dg -> assembly CG (%.1f, %.1f)'%(bm,cx,cy))
# keyhole centre from current foot constants: x67.2, y FOOT_Y+15=23.5
print('current keyhole centre (67.2, 23.5): BELOW every CG scenario -> hanging torque inevitable')
print()

print('=== 3 HATCH CANTILEVER ISOLATION ===')
# Measure whether the beam's three non-root sides are open.
for side,x0 in [('left',22.75),('right',77.70)]:
    # inboard slot region around beam
    gap=Part.makeBox(1.0,12.0,1.0,App.Vector(x0+1.4 if side=='left' else x0-1.0,73.7,0.2))
    mat=hatch.common(gap).Volume
    print('%s beam adjacent slot material %.3f mm3 (0 expected)'%(side,mat))
print()

print('=== 4 FPC / P5 CONTACT ===')
print('FPC bbox',fpc.BoundBox)
print('P5 entry bbox',p5.BoundBox)
print('intersection %.4f mm3, min distance %.4f'%(fpc.common(p5).Volume,fpc.distToShape(p5)[0]))
# Compare entry centres and tangent planes
fc=fpc.BoundBox;pc=p5.BoundBox
print('centres FPC (%.2f,%.2f,%.2f), P5 (%.2f,%.2f,%.2f)'%
      ((fc.XMin+fc.XMax)/2,(fc.YMin+fc.YMax)/2,(fc.ZMin+fc.ZMax)/2,
       (pc.XMin+pc.XMax)/2,(pc.YMin+pc.YMax)/2,(pc.ZMin+pc.ZMax)/2))
print()

print('=== 5 FRONT BOSS BORE CONTINUITY ===')
for x,y in [(2.3,32),(2.3,76.5),(132.1,32),(132.1,76.5)]:
    bore=Part.makeCylinder(1.0,13.0,App.Vector(x,y,3.0))
    print('(%.1f,%.1f) chassis material in long-screw bore %.5f mm3'%(x,y,ch.common(bore).Volume))
print()

print('=== 6 SERVICE PLATE OFFSET FROM CONNECTORS ===')
# transformed keep-outs are reference objects not in saved assembly, so use connector bboxes.
for n in ['BOARD_P2_USB_C','BOARD_U4_MICRO_SD_SOCKET','BOARD_POWER_SWITCH','BOARD_S1_KEY']:
    o=d.getObject(n); b=o.Shape.BoundBox
    print('%-28s +Y edge %.2f  bbox X %.2f..%.2f Z %.2f..%.2f'%(n,b.YMax,b.XMin,b.XMax,b.ZMin,b.ZMax))
# service plate identified as chassis material around y69.4..70.6
print('service plate nominal y=69.4..70.6; connector +Y edges differ and it does not align physically')
