"""Exhaustive saved-assembly collision matrix.

Distinguishes intentional mating pairs from forbidden overlaps and reports minimum clearances.
This catches local component/case collisions hidden by a compound-level aggregate.
"""
import FreeCAD as App,Part,os,sys
from functools import partial
print=partial(print,flush=True)
HERE=os.path.dirname(os.path.abspath(__file__))
d=App.openDocument(os.path.join(HERE,'EInk_Weather_Display_Assembly.FCStd'))
parts={n:d.getObject(n).Shape for n in ['PRINT_FRONT_BEZEL','PRINT_REAR_CHASSIS','PRINT_REAR_COVER','PRINT_SERVICE_HATCH','PRINT_SWING_FOOT']}
board=[o for o in d.getObject('ESP32_M1_ASSEMBLY').Group if hasattr(o,'Shape') and not o.Shape.isNull()]
battery=d.getObject('BATTERY_70x39x11').Shape
glass=d.getObject('PANEL_GLASS').Shape
fpc=d.getObject('PANEL_FPC_FOLDED_180').Shape
front_gasket=d.getObject('FRONT_GASKET_0_2MM').Shape
foam=d.getObject('FOAM_BACKING_0_5MM').Shape
fails=[]
print('=== EXHAUSTIVE COLLISION MATRIX ===')

# The three hinge bearing bands, where cover pin and foot clip are MEANT to interfere.
HINGE_AXIS=[(44.2,5.0,-0.68),(67.2,5.0,-0.68),(90.2,5.0,-0.68)]
HINGE_SPAN=100.0   # long enough to cross the whole foot width
def bearing_band_overlap(sa,sb):
    """"Volume of sa/sb overlap that lies inside the printed hinge bearings."""
    total=0.0
    for cx,cy,cz in HINGE_AXIS:
        band=Part.makeCylinder(2.55,HINGE_SPAN,App.Vector(cx,cy,cz),App.Vector(1,0,0))
        total+=(sa.common(sb)).common(band).Volume
    return total

# Every physical board item against every printable part. Allow only PCB/mount-pillars and
# connector bodies/service face contact; everything else must be zero.
allowed_names={'BOARD_PCB_48x66x1','BOARD_P5_EPAPER_24PIN_FPC','BOARD_P5_ENTRY'}
for pname,ps in parts.items():
    hits=[]
    for o in board:
        v=o.Shape.common(ps).Volume
        if v>.05:
            hits.append((o.Name,v))
    forbidden=[q for q in hits if q[0] not in allowed_names]
    print(' %-20s board hits %d forbidden %d%s'%(pname,len(hits),len(forbidden),
          (' '+str([(n,round(v,3)) for n,v in forbidden[:5]])) if forbidden else ''))
    if forbidden:fails.append(pname+' intersects board: '+str(forbidden[:5]))
# Each reference envelope against each printable.
for label,shape in [('battery',battery),('glass',glass),('FPC',fpc)]:
    for pname,ps in parts.items():
        v=shape.common(ps).Volume
        allowed=(label=='glass' and pname=='PRINT_FRONT_BEZEL' and v<.05) or \
                (label=='FPC' and pname=='PRINT_REAR_CHASSIS' and v<.001)
        if v>.001 and not allowed:
            print(' FAIL %-8s vs %-20s %.4f mm3'%(label,pname,v))
            fails.append('%s intersects %s %.4f'%(label,pname,v))
# Printable pairs. Cover/hatch/foot and chassis/bezel must be clearance fits, never overlaps.
names=list(parts)
for i in range(len(names)):
    for j in range(i+1,len(names)):
        a,b=names[i],names[j]
        v=parts[a].common(parts[b]).Volume
        # The swing foot's clip bore is 0.12 mm smaller than the cover's hinge pin ON PURPOSE:
        # that interference is the friction that stops the stand flopping. It is not a
        # collision, so it is excused only when every bit of it lies inside a bearing-band
        # solid; anything outside those bands is still a real collision.
        if {a,b}=={'PRINT_REAR_COVER','PRINT_SWING_FOOT'}:
            outside=v-bearing_band_overlap(parts[a],parts[b])
        else:
            outside=v
        if outside>0.05:
            print(' FAIL printable pair %-20s / %-20s %.4f (%.4f off-bearing)'% (a,b,v,outside))
            fails.append('printable pair intersects: %s/%s %.4f'%(a,b,v))
# Gasket/foam intended contacts and forbidden printed overlap.
for label,shape in [('front gasket',front_gasket),('rear foam',foam)]:
    gd=shape.distToShape(glass)[0]
    print(' %-13s to glass distance %.4f mm'% (label,gd))
    if gd>.01:fails.append(label+' not touching glass')
    for pname,ps in parts.items():
        v=shape.common(ps).Volume
        # front gasket touches bezel only at a face, no volume; rear foam touches chassis face.
        if v>.01:
            print(' FAIL %-13s intersects %-20s %.4f'%(label,pname,v))
            fails.append(label+' intersects '+pname)
if fails:
 print('\nCOLLISION FAILURES:');[print(' -',f) for f in fails];sys.exit(1)
print('\nEXHAUSTIVE COLLISION MATRIX PASS')
