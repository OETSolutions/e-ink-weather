"""Validate the front-driven screw chain end to end, as an actual assembly.

The prior design failed this: blind pilots with no reachable driver path. This test models the
real hardware and asserts the whole chain, not just that holes exist.

Chain: M2 x 10 screw -> counterbore + through-hole in bezel -> through-hole in chassis boss
       -> M2 hex nut in a captive pocket.
"""
import FreeCAD as App,Part,os,sys,math
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import CASE_D,LIP_UNDERSIDE,SPLIT_Z,GRID_Z0
from functools import partial
print=partial(print,flush=True)
HERE=os.path.dirname(os.path.abspath(__file__))
d=App.openDocument(os.path.join(HERE,'EInk_Weather_Display_Assembly.FCStd'))
bez=d.getObject('PRINT_FRONT_BEZEL').Shape
ch=d.getObject('PRINT_REAR_CHASSIS').Shape
PTS=[(2.3,32.0),(2.3,76.5),(132.1,32.0),(132.1,76.5)]
HEAD_D=4.0; HEAD_T=1.2; SHANK_D=2.0; SHANK_L=8.0
fails=[]
print('=== FRONT-DRIVEN FASTENER CHAIN ===')

for x,y in PTS:
    # 1. Driver path: a 6 mm driver body must reach the head counterbore from the front.
    drv=Part.makeCylinder(3.0,12.0,App.Vector(x,y,CASE_D-0.3))
    drv_block=bez.common(drv).Volume
    # 2. Counterbore present and deep enough for the head.
    cb=Part.makeCylinder(HEAD_D/2+0.3,HEAD_T,App.Vector(x,y,CASE_D-HEAD_T))
    cb_fill=bez.common(cb).Volume/cb.Volume
    # 3. Through-hole through the full bezel band.
    th=Part.makeCylinder(SHANK_D/2+0.15,LIP_UNDERSIDE-SPLIT_Z,App.Vector(x,y,SPLIT_Z))
    th_fill=bez.common(th).Volume/th.Volume
    # 4. Chassis boss must exist and be open on the same axis. Judge by material per mm of
    # height so a legitimate depth-stack change does not silently fail a raw mm3 gate.
    boss=Part.makeCylinder(3.4,GRID_Z0-6.0,App.Vector(x,y,6.0))
    boss_mat=ch.common(boss).Volume
    boss_density=boss_mat/(GRID_Z0-6.0)
    ch_hole=Part.makeCylinder(SHANK_D/2+0.15,GRID_Z0-5.0,App.Vector(x,y,5.0))
    ch_fill=ch.common(ch_hole).Volume/ch_hole.Volume
    # 5. Nut pocket must be open and large enough across flats (M2 nut AF 4.0).
    nut=Part.makeCylinder(4.6/2/math.cos(math.radians(30)),1.6,App.Vector(x,y,6.2))
    nut_fill=ch.common(nut).Volume/nut.Volume
    ok=(drv_block<0.6 and cb_fill<0.05 and th_fill<0.05 and boss_density>20.0
        and ch_fill<0.10 and nut_fill<0.10)
    print(' (%.1f,%.1f) driver %.3f cbores %.2f bezel_hole %.2f boss %.0f (%.1f/mm) ch_hole %.2f nut %.2f %s'%
          (x,y,drv_block,cb_fill,th_fill,boss_mat,boss_density,ch_fill,nut_fill,'OK' if ok else 'FAIL'))
    if not ok:fails.append('fastener chain failed at %.1f,%.1f'%(x,y))

# 6. Full screw swept into place must not collide with anything.
print()
print('=== SCREW INSERTION SWEEP ===')
for x,y in PTS:
    # Screw seated: head bottom at CASE_D-HEAD_T, shank descending 8 mm.
    for offset in [0.0,1.0,2.0,3.0]:
        z_head_bottom=CASE_D-HEAD_T+offset
        head=Part.makeCylinder(HEAD_D/2,HEAD_T,App.Vector(x,y,z_head_bottom))
        shank=Part.makeCylinder(SHANK_D/2,SHANK_L,
                                App.Vector(x,y,z_head_bottom-SHANK_L))
        screw=head.fuse(shank)
        clash=bez.common(screw).Volume+ch.common(screw).Volume
        if offset==0.0:
            seated=clash
        if clash>seated+0.01 and offset<3.0:
            print('  FAIL screw at %.1f,%.1f collides during travel (+%.1f mm): %.3f'%
                  (x,y,offset,clash))
            fails.append('screw travel collision at %.1f,%.1f'%(x,y))
    print('  (%.1f,%.1f) seated interference %.3f mm3 %s'%
          (x,y,seated,'OK' if seated<0.2 else 'FAIL'))
    if seated>=0.2:fails.append('seated screw interferes at %.1f,%.1f'%(x,y))

if fails:
    print('\nFASTENER FAILURES:');[print(' -',f) for f in fails];sys.exit(1)
print('\nFRONT-DRIVEN FASTENER CHAIN PASS')
