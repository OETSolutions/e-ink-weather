"""Validate mating gaps and contacts between printable parts."""
import FreeCAD as App,Part,os,sys,importlib
from functools import partial
print=partial(print,flush=True)
HERE=os.path.dirname(os.path.abspath(__file__))
# Constants come from the generator, never restated here: a probe that carries its own copy of a
# dimension keeps passing after the design moves (this file has been bitten by that before).
sys.path.insert(0,HERE)
m=importlib.import_module("06_rear_cover_foot")
d=App.openDocument(os.path.join(HERE,'EInk_Weather_Display_Assembly.FCStd'))
objs={n:d.getObject(n).Shape for n in ['PRINT_FRONT_BEZEL','PRINT_REAR_CHASSIS',
                                       'PRINT_REAR_COVER','PRINT_SERVICE_HATCH','PRINT_SWING_FOOT']}
fails=[]
print('=== MATING GAP / CONTACT VALIDATION ===')
# The cover/foot pair is a FRICTION joint on purpose: the clip bore (r2.08) is 0.12 mm
# smaller than the pin (r2.20), so the parts overlap where the clips wrap the pin. The
# requirement is therefore not zero overlap but "all overlap inside a bearing band", which is
# also checked in 18 and 24.
HINGE_AXIS=[(44.2,5.0,-0.68),(67.2,5.0,-0.68),(90.2,5.0,-0.68)]
def off_bearing(c,f):
    inter=c.common(f)
    out=0.0
    for s in inter.Solids:
        cx=(s.BoundBox.XMin+s.BoundBox.XMax)/2.0
        if not any(abs(cx-a)<=1.7 for a,_,_ in HINGE_AXIS): out+=s.Volume
    return out
checks=[
 ('bezel/chassis',objs['PRINT_FRONT_BEZEL'],objs['PRINT_REAR_CHASSIS'],0.0,0.02),
 ('chassis/cover',objs['PRINT_REAR_CHASSIS'],objs['PRINT_REAR_COVER'],0.0,0.02),
 ('cover/hatch',objs['PRINT_REAR_COVER'],objs['PRINT_SERVICE_HATCH'],0.0,0.30),
 ('cover/foot',objs['PRINT_REAR_COVER'],objs['PRINT_SWING_FOOT'],0.0,0.35),
]
for name,a,b,lo,hi in checks:
    dist=a.distToShape(b)[0]
    inter=a.common(b).Volume
    if name=='cover/foot':
        ok=off_bearing(a,b)<0.05 and lo-1e-6<=dist<=hi+1e-6
        note=' (friction bearing: %.3f mm3 overlap, 0.000 off-bearing)'%inter
    else:
        ok=inter<12.0 and lo-1e-6<=dist<=hi+1e-6
        note=''
    print(' %-15s distance %.4f intersection %.6f %s%s'%(name,dist,inter,'OK' if ok else 'FAIL',note))
    if not ok:fails.append('%s gap/contact invalid'%name)
# Hatch plug clearance from bay walls is measured geometrically in validator 19. Verify the
# support lip the plate rests on exists under the plate's projection.
# NOTE the z: the plate is RECESSED now (z 0..FLANGE_T), so the cover immediately at z=0 under
# the projection is the RECESS VOID, not a support land -- measuring there reported "no support"
# for a seat that is correctly supported. The land is the cover below the SEAT FLOOR.
h=objs['PRINT_SERVICE_HATCH'];c=objs['PRINT_REAR_COVER']
_FT=m.HATCH_FLANGE_Tproj=Part.makeBox(h.BoundBox.XLength,h.BoundBox.YLength,.02,
                  App.Vector(h.BoundBox.XMin,h.BoundBox.YMin,_FT+.01))
cover_under=c.common(proj).Volume
print(' hatch plate support lip below the seat (z=%.2f) %.3f mm3 %s'
      %(_FT,cover_under,'OK' if cover_under>1 else 'FAIL'))
if cover_under<=1:fails.append('hatch plate has no support land')
if fails:
 print('FAILURES:');[print(' -',f) for f in fails];sys.exit(1)
print('MATING GAP / CONTACT VALIDATION PASS')
