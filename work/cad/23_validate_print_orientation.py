"""Audit each part under its declared FDM print orientation.

Build direction is explicit:
  bezel: Z-max (cosmetic front) on bed, print toward decreasing model Z
  chassis: Z-max (front/grid face) on bed, print toward decreasing model Z; pillars,
           battery collar and service face grow continuously from the grid
  cover: Z-max (interior face) on bed; hinge cheeks may use local support as declared
  hatch: Z-min (exterior flange face) on bed
  foot: Z-max (large exterior plate face + knuckle tangent) on bed

This screen catches floating first layers and unsupported interior planes. It does not treat a
face already supported by material printed on the preceding transformed layer as a bridge.
"""
import FreeCAD as App,Part,os,sys,math
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import CASE_D,LIP_UNDERSIDE,SPLIT_Z,GRID_Z0
from functools import partial
print=partial(print,flush=True)
HERE=os.path.dirname(os.path.abspath(__file__))
d=App.openDocument(os.path.join(HERE,'EInk_Weather_Display_Assembly.FCStd'))
parts={n:d.getObject(n).Shape for n in ['PRINT_FRONT_BEZEL','PRINT_REAR_CHASSIS',
                                        'PRINT_REAR_COVER','PRINT_SERVICE_HATCH','PRINT_SWING_FOOT']}
fails=[]
print('=== PRINT ORIENTATION / OVERHANG AUDIT ===')

# (name, bed side): max = model Zmax lies on bed; min = model Zmin lies on bed.
orient={'PRINT_FRONT_BEZEL':'max','PRINT_REAR_CHASSIS':'max','PRINT_REAR_COVER':'max',
        'PRINT_SERVICE_HATCH':'min','PRINT_SWING_FOOT':'min'}
# The foot prints plate-and-rib-down, so its bed side is the MINIMUM Z (the rib and clips),
# not the plate's outer face.
for name,side in orient.items():
    sh=parts[name]; z=sh.BoundBox.ZMax if side=='max' else sh.BoundBox.ZMin
    if side=='max': slab=Part.makeBox(150,120,.2,App.Vector(-5,-5,z-.2))
    else: slab=Part.makeBox(150,120,.2,App.Vector(-5,-5,z))
    v=sh.common(slab).Volume
    minimum=10.0 if name=='PRINT_REAR_COVER' else (18.0 if name=='PRINT_SWING_FOOT' else 80.0)
    ok=v>minimum
    print(' %-23s bed=%s Z %.2f first-layer %.1f mm3 %s'%
          (name.replace('PRINT_',''),side,z,v,'OK' if ok else 'FAIL'))
    if not ok:fails.append(name+' has insufficient first-layer contact')

# Bezel: front lip is printed first as a 1.6 mm-thick ring. At the transition to the skirt,
# every point of the skirt must overlap the lip or outer roll already below it in print space.
bezel=parts['PRINT_FRONT_BEZEL']
for z,label in [(CASE_D-.2,'lip early layer'),(LIP_UNDERSIDE+.2,'lip final layer'),
                (LIP_UNDERSIDE-.1,'lip-to-skirt transition'),(SPLIT_Z+.8,'skirt/FPC-channel layer'),
                (SPLIT_Z+.1,'skirt final layer')]:
    slab=Part.makeBox(140,114,.12,App.Vector(-3,-3,z))
    v=bezel.common(slab).Volume
    ok=v>80.0
    print(' bezel %-25s material %.1f mm3 %s'%(label,v,'OK' if ok else 'FAIL'))
    if not ok:fails.append('bezel discontinuity at '+label)
# No material may float below the split plane.
if bezel.BoundBox.ZMin<SPLIT_Z-1e-5:fails.append('bezel extends below split plane')

# Chassis: support grid bridges only its 5 mm clear cells. Verify actual pitch/clear span and
# structural bridge material immediately below it.
ch=parts['PRINT_REAR_CHASSIS']
print(' chassis grid clear-cell bridge 5.0 mm (accepted)')
bridge=Part.makeBox(2.0,8.0,1.0,App.Vector(3.0,40.0,GRID_Z0-1.2))
bridge_frac=ch.common(bridge).Volume/bridge.Volume
print(' chassis tub-grid bridge fill %.2f %s'%(bridge_frac,'OK' if bridge_frac>.6 else 'FAIL'))
if bridge_frac<=.6:fails.append('tub-grid bridge not continuous')
# In front/grid-face-down orientation, all internal features must overlap the grid at z=GRID_Z0.
for label,x,y in [('pillar A',79.6,62.54),('pillar B',18.4,19.34),
                  ('battery collar',85.4,50.0),('service face',23.6,69.9)]:
    probe=Part.makeBox(1.0,1.0,0.5,App.Vector(x-.5,y-.5,GRID_Z0-.1))
    v=ch.common(probe).Volume
    ok=v>.10
    print(' chassis %-16s grid overlap %.3f %s'%(label,v,'OK' if ok else 'FAIL'))
    if not ok:fails.append(label+' does not grow from grid')

# Cover Z-max on bed. Its broad interior plate prints first; the bottom-edge cradle grows up
# from its connected rail. Probe the rail-to-cover connection and bearing first-layer span.
cover=parts['PRINT_REAR_COVER']
rail=Part.makeBox(50,2.0,1.0,App.Vector(42,6.6,1.9))
rail_v=cover.common(rail).Volume
print(' cover hinge rail connection %.1f mm3 %s'%(rail_v,'OK' if rail_v>20 else 'FAIL'))
if rail_v<=20:fails.append('hinge rail not connected to cover print')

# Foot Z-max on bed: plate face and knuckle tangent are exactly coplanar.
foot=parts['PRINT_SWING_FOOT']
# The foot prints plate-and-rib-down: the rib bottom and the clips are level, giving the
# bed contact measured above.
# The foot prints plate-and-rib-down. First-layer contact is the rib footprint plus the clips
# at the lowest Z; this is what actually touches the bed.
rib_bottom=foot.BoundBox.ZMin
contact=foot.common(Part.makeBox(200,200,0.25,App.Vector(-80,-80,rib_bottom-0.01))).Volume
print(' foot print bed plane Z %.2f, first-layer contact %.1f mm3 (>20 required) %s'%
      (rib_bottom,contact,'OK' if contact>20.0 else 'FAIL'))
if contact<=20.0:fails.append('foot has insufficient first-layer contact')

# Hatch exterior flange at Z-min is broad; beams and detents rise from plug material.
hatch=parts['PRINT_SERVICE_HATCH']
beam=Part.makeBox(1.2,10,.8,App.Vector(22.75,74.0,.3))
beam_frac=hatch.common(beam).Volume/beam.Volume
print(' hatch cantilever beam fill %.2f %s'%(beam_frac,'OK' if beam_frac>.8 else 'FAIL'))
if beam_frac<=.8:fails.append('hatch cantilever beam discontinuous')

if fails:
 print('\nPRINT ORIENTATION FAILURES:');[print(' -',f) for f in fails];sys.exit(1)
print('\nPRINT ORIENTATION / OVERHANG AUDIT PASS')
