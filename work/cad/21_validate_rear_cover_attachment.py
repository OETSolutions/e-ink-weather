"""Validate rear-cover-to-chassis attachment as a real mechanical interface."""
import FreeCAD as App,Part,os,sys,math
from functools import partial
print=partial(print,flush=True)
HERE=os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0,HERE)
from enclosure_dimensions import M2_CLEAR,M2_HEAD,M2_PILOT,M2_ENGAGE
d=App.openDocument(os.path.join(HERE,'EInk_Weather_Display_Assembly.FCStd'))
cover=d.getObject('PRINT_REAR_COVER').Shape
ch=d.getObject('PRINT_REAR_CHASSIS').Shape
PTS=[(11,11),(123.4,11),(11,97.5),(123.4,97.5)]
fails=[]
print('=== REAR COVER ATTACHMENT VALIDATION ===')
for x,y in PTS:
    # Cover should be open around the M2 clearance radius across full 3 mm thickness, and have
    # a head recess. Radii come from the design's own M2 constants, not hardcoded M2.5 values.
    shank=Part.makeCylinder(M2_CLEAR/2,3.4,App.Vector(x,y,-.2))
    blocked=cover.common(shank).Volume
    head=Part.makeCylinder(M2_HEAD/2,1.2,App.Vector(x,y,-.1))
    head_block=cover.common(head).Volume
    # Chassis pilot is open at the M2 self-tap pilot radius for the full engagement depth,
    # surrounded by the boss wall.
    pilot=Part.makeCylinder(M2_PILOT/2,M2_ENGAGE,App.Vector(x,y,3.0))
    pilot_mat=ch.common(pilot).Volume
    boss_ring=Part.makeCylinder(4.0,M2_ENGAGE,App.Vector(x,y,3.0)).cut(
              Part.makeCylinder(M2_PILOT/2,M2_ENGAGE+0.4,App.Vector(x,y,2.8)))
    ring_mat=ch.common(boss_ring).Volume
    # Each boss must be connected to chassis beyond itself: material in the X and Y gussets.
    gus=Part.makeBox(10,3,3,App.Vector(x-5,y-1.5,3.5)).fuse(
        Part.makeBox(3,10,3,App.Vector(x-1.5,y-5,3.5)))
    gus_mat=ch.common(gus).Volume
    ok=blocked<.01 and head_block<.01 and pilot_mat<.01 and ring_mat>100 and gus_mat>40
    print(' (%.1f,%.1f) cover shank %.4f head %.4f; chassis pilot %.4f ring %.1f gusset %.1f %s'%
          (x,y,blocked,head_block,pilot_mat,ring_mat,gus_mat,'OK' if ok else 'FAIL'))
    if not ok:fails.append('rear-cover interface failed at %.1f,%.1f'%(x,y))
if fails:
 print('FAILURES:');[print(' -',f) for f in fails];sys.exit(1)
print('REAR COVER ATTACHMENT VALIDATION PASS')
