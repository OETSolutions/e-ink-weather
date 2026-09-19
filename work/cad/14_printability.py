"""Broad FDM material probes for every printable part.

`20_validate_min_thickness.py` performs the critical local-thickness measurements. This file
adds broad fill/continuity probes so a nominally thick feature that was accidentally severed
still fails.
"""
import FreeCAD as App, Part, os, sys
from functools import partial
print=partial(print,flush=True)
HERE=os.path.dirname(os.path.abspath(__file__))
d=App.openDocument(os.path.join(HERE,"EInk_Weather_Display_Assembly.FCStd"))

checks=[]
def probe(name,box_dims,origin,label,min_fill=0.6):
    b=Part.makeBox(*box_dims,App.Vector(*origin))
    v=d.getObject(name).Shape.common(b).Volume
    frac=v/(box_dims[0]*box_dims[1]*box_dims[2])
    ok=frac>min_fill
    print("  %-44s fill %.2f %s"%(label,frac,"OK" if ok else "FAIL"))
    if not ok:checks.append(label)

print("=== FDM PRINTABILITY / CONTINUITY PROBES ===")
# Front retaining lip: actual 1.6 mm band, not generic skirt.
probe("PRINT_FRONT_BEZEL",(1.0,10.0,1.4),(4.5,40.0,17.5),"front retaining lip solid",0.55)
# Bezel skirt.
probe("PRINT_FRONT_BEZEL",(1.2,2.0,2.0),(1.2,53.0,15.9),"bezel side skirt solid")
# Chassis perimeter wall at mid-height.
probe("PRINT_REAR_CHASSIS",(1.2,2.0,2.0),(0.3,53.0,11.0),"chassis perimeter wall solid")
# Support grid rib at actual z14.6..15.8.
probe("PRINT_REAR_CHASSIS",(0.6,4.0,1.0),(11.7,9.4,14.65),"support grid rib solid")
# 1.4 mm tub-grid bridge away from FPC notch.
probe("PRINT_REAR_CHASSIS",(2.0,8.0,1.0),(3.0,40.0,13.35),"tub-to-grid bridge solid",0.35)
# Real rear-cover boss and gusset.
probe("PRINT_REAR_CHASSIS",(4.0,4.0,3.0),(9.0,9.0,3.2),"rear-cover boss/gusset solid",0.25)
# 1.2 mm cover skin behind the foot pocket. The pocket opens from z=0 to 1.8; the skin is
# therefore z=1.8..3.0, not at the exterior face.
probe("PRINT_REAR_COVER",(2.0,2.0,1.0),(40.0,20.0,1.9),"cover pocket skin solid",0.9)
# Ordinary cover plate outside openings.
probe("PRINT_REAR_COVER",(2.0,2.0,1.0),(100.0,80.0,0.4),"cover plate solid")
# Foot plate.
probe("PRINT_SWING_FOOT",(3.0,3.0,1.2),(60.0,30.0,0.2),"foot plate solid")
# Hatch plug and latch beam.
probe("PRINT_SERVICE_HATCH",(2.0,2.0,1.0),(50.0,80.0,0.3),"hatch plug solid")
probe("PRINT_SERVICE_HATCH",(1.0,4.0,0.8),(22.8,78.0,0.3),"left latch beam solid",0.7)
# Board standoff column.
probe("PRINT_REAR_CHASSIS",(2.0,2.0,1.8),(78.6,61.5,12.4),"board standoff column solid",0.35)
# Battery collar wall.
probe("PRINT_REAR_CHASSIS",(0.8,6.0,0.8),(84.9,40.0,13.5),"battery collar wall solid",0.25)

if checks:
    print("PRINTABILITY FAILURES:",checks);sys.exit(1)
print("FDM PRINTABILITY PROBES PASS")
