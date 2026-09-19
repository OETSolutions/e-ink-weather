"""Measure every hinge and hatch feature against documented FDM minimums.

Documented values (0.4 mm nozzle, PETG):
  min slot/groove width        0.8 mm   (below this the slicer welds it shut)
  min wall/flexure thickness   1.2 mm   (3 extrusion lines)
  hand-assembled moving fit    0.20-0.30 mm per side
  snap undercut on latch face  0.10-0.20 mm
  snap arm L:h ratio           >= 2:1, target 3:1
  strain e = 1.5*h*Y/L^2       PETG allowable 0.03-0.05
"""
import FreeCAD as App,Part,os,sys,math
from functools import partial
print=partial(print,flush=True)
cad=os.path.dirname(os.path.abspath(__file__))
d=App.openDocument(os.path.join(cad,'rear_cover_foot.FCStd'))
cov=d.getObject('PRINT_REAR_COVER').Shape
foot=d.getObject('PRINT_FOOT').Shape
hatch=d.getObject('PRINT_SERVICE_HATCH').Shape
MIN_SLOT=0.8; MIN_WALL=1.2
print('=== HATCH LATCH SLOT WIDTH (min %.1f mm) ==='%MIN_SLOT)
# The U-slots release each beam. Measure the open gap between beam and plug.
for x in [22.0,23.0,24.0,25.0,26.0,27.0]:
    col=Part.makeBox(0.05,14.0,1.0,App.Vector(x,73.0,0.15))
    v=hatch.common(col).Volume
    print('  x=%5.1f material %.4f %s'%(x,v,'beam' if v>0.1 else ('OPEN' if v<0.01 else 'partial')))
print()
print('=== HATCH BEAM THICKNESS (min %.1f mm) ==='%MIN_WALL)
# sweep across the beam to find its width in X
runs=[];prev=None;start=None
for i in range(0,400):
    x=20.0+i*0.05
    col=Part.makeBox(0.05,12.0,0.9,App.Vector(x,74.0,0.2))
    solid=hatch.common(col).Volume>0.30
    if solid and prev!=True: start=x
    if not solid and prev==True: runs.append((start,x,x-start))
    prev=solid
print('  solid X runs (width):',[('%.2f-%.2f w%.2f'%r) for r in runs if r[2]>0.1])
print()
print('=== HINGE STOP LUG / STRUT THICKNESS (min %.1f mm) ==='%MIN_WALL)
# stop lugs are at cover x bands 29.2-35.2 and 96.2-102.2, y 5.95..7.25, z -4.2..-3.5
for label,x0 in [('left lug',29.2),('right lug',96.2)]:
    col=Part.makeBox(6.0,1.30,0.70,App.Vector(x0,5.95,-4.20))
    v=cov.common(col).Volume
    print('  %-10s nominal 6.0 x 1.30 x 0.70 -> %.1f mm3 (thickness 0.70 <= %.1f!)'%(label,v,MIN_WALL))
print()
print('=== HINGE BEARING CLEARANCE (want 0.20-0.30 per side) ===')
KN_R,CR_R=2.20,2.50
print('  knuckle r %.2f, bore r %.2f, radial clearance %.2f mm %s'%
      (KN_R,CR_R,CR_R-KN_R,'OK' if 0.20<=CR_R-KN_R<=0.32 else 'CHECK'))
print()
print('=== SNAP MOUTH: how much must the ring deflect? ===')
MOUTH_UNDERCUT=0.40
print('  designed diametral undercut %.2f mm -> %.2f mm per side'%(MOUTH_UNDERCUT,MOUTH_UNDERCUT/2))
print('  documented snap-face allowance is 0.10-0.20 mm TOTAL, so this is 2-4x too aggressive')
print('  and the C-ring has only ~210 deg of material, so it cannot flex as a ring at all.')
print()
print('=== NECKS (min %.1f mm) ==='%MIN_WALL)
ARM_W,ARM_T=6.0,1.5
print('  neck %.1f wide x %.1f thick %s'%(ARM_W,ARM_T,'OK' if min(ARM_W,ARM_T)>=MIN_WALL else 'FAIL'))
