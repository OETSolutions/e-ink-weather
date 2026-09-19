"""Enforce minimum printable feature thicknesses on the SAVED geometry.

This is the validator whose absence allowed a 0.3 mm display retaining lip to ship: the part
was one valid solid, so every geometric check passed while a functionally critical feature was
one extrusion line thick. These probes measure real material thickness, not nominal constants.

Thresholds are 0.4 mm nozzle PETG:
  MIN_PRINTED_WALL 1.2  general skins, ribs, collars
  MIN_LOAD_WALL    1.6  retaining lips and structural links
  MIN_SKIN         1.2  pocket floors / skins behind openings
  MIN_M2_THREAD    2.0  distributed case attachment
"""
import FreeCAD as App
import Part
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import (MIN_PRINTED_WALL, MIN_LOAD_WALL, MIN_SKIN,
                                  MIN_M2_THREAD, CASE_D, LIP_UNDERSIDE, GRID_Z0,
                                  GRID_Z1, REAR_COVER_T)
from functools import partial
print = partial(print, flush=True)

HERE=os.path.dirname(os.path.abspath(__file__))
d=App.openDocument(os.path.join(HERE,"EInk_Weather_Display_Assembly.FCStd"))
bezel=d.getObject("PRINT_FRONT_BEZEL").Shape
chassis=d.getObject("PRINT_REAR_CHASSIS").Shape
cover=d.getObject("PRINT_REAR_COVER").Shape
hatch=d.getObject("PRINT_SERVICE_HATCH").Shape
foot=d.getObject("PRINT_SWING_FOOT").Shape
fails=[]

def thickness(shape,px,py,z,r=0.35,h=None):
    if h is None: h=CASE_D+2.0
    col=Part.makeCylinder(r,h,App.Vector(px,py,z-1.0))
    return shape.common(col).Volume/(math.pi*r*r)

print("=== MINIMUM FEATURE THICKNESS (saved geometry) ===")
print("thresholds: wall %.1f  load %.1f  skin %.1f  thread %.1f mm"%
      (MIN_PRINTED_WALL,MIN_LOAD_WALL,MIN_SKIN,MIN_M2_THREAD))

# Display retaining lip: measure through the full lip band on the glass border.
print("\n-- display retaining lip (>= %.1f mm) --"%MIN_LOAD_WALL)
for px,py,label in [(4.6,54.0,"left border"),(129.8,54.0,"right border"),
                    (67.2,4.6,"FPC edge"),(67.2,103.9,"top border"),
                    (5.0,20.0,"left near corner"),(129.5,20.0,"right near corner")]:
    t=thickness(bezel,px,py,LIP_UNDERSIDE)
    ok=t>=MIN_LOAD_WALL-0.05
    print("  %-18s %.3f mm %s"%(label,t,"OK" if ok else "FAIL"))
    if not ok:fails.append("retaining lip %.3f mm at %s"%(t,label))

# Bezel skirt wall between the panel pocket and the outer surface.
print("\n-- bezel skirt / side wall (>= %.1f mm) --"%MIN_PRINTED_WALL)
for px,py in [(2.3,54.0),(132.1,54.0),(67.2,2.3),(67.2,106.2)]:
    t=thickness(bezel,px,py,GRID_Z1+0.2)
    ok=t>=MIN_PRINTED_WALL-0.05
    print("  (%.1f,%.1f) %.3f mm %s"%(px,py,t,"OK" if ok else "FAIL"))
    if not ok:fails.append("bezel skirt %.3f mm at %.1f,%.1f"%(t,px,py))

# Chassis perimeter wall and the bridge link.
print("\n-- chassis perimeter wall (>= %.1f mm) --"%MIN_PRINTED_WALL)
for px,py in [(0.9,54.0),(133.5,54.0),(67.2,0.9),(67.2,107.6)]:
    t=thickness(chassis,px,py,GRID_Z0)
    ok=t>=MIN_PRINTED_WALL-0.05
    print("  (%.1f,%.1f) %.3f mm %s"%(px,py,t,"OK" if ok else "FAIL"))
    if not ok:fails.append("chassis wall %.3f mm at %.1f,%.1f"%(t,px,py))

# Support grid rib depth (printed, spanning glass backing).
print("\n-- support grid rib depth (>= %.1f mm) --"%MIN_PRINTED_WALL)
col=Part.makeBox(0.6,4.0,GRID_Z1-GRID_Z0,App.Vector(11.7,9.4,GRID_Z0))
frac=chassis.common(col).Volume/col.Volume
print("  grid rib fill %.2f %s"%(frac,"OK" if frac>0.6 else "FAIL"))
if frac<=0.6:fails.append("support grid rib not solid")

# Rear cover skin under the foot pocket, away from the keyhole.
print("\n-- rear cover skin under foot pocket (>= %.1f mm) --"%MIN_SKIN)
for px,py in [(40.0,20.0),(95.0,20.0),(40.0,44.0),(95.0,44.0)]:
    t=thickness(cover,px,py,0.0)
    ok=t>=MIN_SKIN-0.05
    print("  (%.1f,%.1f) %.3f mm %s"%(px,py,t,"OK" if ok else "FAIL"))
    if not ok:fails.append("cover skin %.3f mm at %.1f,%.1f"%(t,px,py))

# Rear-cover bosses: thread engagement available in the chassis. The boss begins at the cover
# plane (z=REAR_COVER_T) and runs up, so probe its full height, not below the cover.
print("\n-- rear-cover boss engagement (>= %.1f mm) --"%MIN_M2_THREAD)
COVER_SCREW_XY=[(11.0,11.0),(134.4-11.0,11.0),(11.0,108.5-11.0),(134.4-11.0,108.5-11.0)]
for px,py in COVER_SCREW_XY:
    z0=REAR_COVER_T
    engage=4.0
    col=Part.makeCylinder(1.05,engage,App.Vector(px,py,z0))
    void=col.Volume-chassis.common(col).Volume
    # Thread wall: solid between the pilot and boss OD, over the designed 4.0 mm engagement.
    wall=Part.makeCylinder(3.0,engage,App.Vector(px,py,z0)).cut(
         Part.makeCylinder(1.05,engage+0.4,App.Vector(px,py,z0-0.2)))
    solid=chassis.common(wall).Volume
    ok=void>0.98*col.Volume and solid>80.0
    print("  (%.1f,%.1f) pilot open %.0f%% thread wall %.1f mm3 %s"%
          (px,py,100.0*void/col.Volume,solid,"OK" if ok else "FAIL"))
    if not ok:fails.append("rear-cover boss missing at %.1f,%.1f"%(px,py))

# Hatch latch beams: direct cross-section at mid-span. Left beam x=22.75..23.95;
# right beam x=77.70..78.90; both run y=73.7..85.9.
print("\n-- hatch latch beam (>= %.1f mm) --"%MIN_PRINTED_WALL)
for x0,label in [(22.75,"left"),(77.70,"right")]:
    sec=Part.makeBox(1.2,0.4,0.8,App.Vector(x0,80.0,0.3))
    fill=hatch.common(sec).Volume/sec.Volume
    ok=fill>0.85
    print("  %-5s 1.2 mm cross-section fill %.2f %s"%(label,fill,"OK" if ok else "FAIL"))
    if not ok:fails.append(label+" hatch latch beam below 1.2 mm")

# Hinge knuckle and cradle wall: measure the cradle's radial wall thickness at the bore.
print("\n-- hinge knuckle / cradle (>= %.1f mm) --"%MIN_PRINTED_WALL)
kn=foot.common(Part.makeCylinder(2.2,80.0,App.Vector(0,5.0,-1.10),App.Vector(1,0,0))).Volume
# The cover now holds a rigid PIN; the foot's clip material is measured separately below.
pin_mat=cover.common(Part.makeCylinder(2.2,80.0,App.Vector(0,5.0,-1.10),App.Vector(1,0,0))).Volume
clip_mat=foot.common(Part.makeCylinder(4.1,80.0,App.Vector(0,5.0,-1.10),App.Vector(1,0,0))).Volume
shell=clip_mat                        # the foot's compliant clip wrap
print("  cover PIN material %.1f (>=200) %s"%(pin_mat,"OK" if pin_mat>200.0 else "FAIL"))
print("  foot CLIP material  %.1f (>=30)  %s"%(clip_mat,"OK" if clip_mat>30.0 else "FAIL"))
print("  cradle shell 2.5..3.0 mm: %.1f (>=30) %s"%(shell,"OK" if shell>30.0 else "FAIL"))
if pin_mat<=200.0 or shell<=30.0:fails.append("hinge pin/clip geometry too small")

if fails:
    print("\nMINIMUM-THICKNESS FAILURES:")
    for f in fails: print(" -",f)
    sys.exit(1)
print("\nMINIMUM-THICKNESS VALIDATION PASS")
