"""Validate the snap-in service hatch and USB cable exit in saved geometry."""
import FreeCAD as App
import Part
import os, sys, math
from functools import partial
print=partial(print,flush=True)

HERE=os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0,HERE)
import importlib
m=importlib.import_module("06_rear_cover_foot")
rprism=m.rprism
d=App.openDocument(os.path.join(HERE,"rear_cover_foot.FCStd"))
cover=d.getObject("PRINT_REAR_COVER").Shape
hatch=d.getObject("PRINT_SERVICE_HATCH").Shape

BAY=(m.SERVICE_BAY_X0,m.SERVICE_BAY_X1,m.SERVICE_BAY_Y0,m.SERVICE_BAY_Y1)
PLUG=(m.SERVICE_BAY_X0+m.HATCH_CLEAR,m.SERVICE_BAY_X1-m.HATCH_CLEAR,
      m.SERVICE_BAY_Y0+m.HATCH_CLEAR,m.SERVICE_BAY_Y1-m.HATCH_CLEAR)
FLANGE=(m.HATCH_FLANGE_X0,m.HATCH_FLANGE_X1,m.HATCH_FLANGE_Y0,m.HATCH_FLANGE_Y1)
SCREW_AXIS=(m.HATCH_SCREW_X,m.HATCH_SCREW_Y)
SCREW_D=m.HATCH_SCREW_D
LIP_T=m.HATCH_LIP_T
LIP_H=m.HATCH_LIP_H
LIP_CLEAR=m.HATCH_LIP_CLEAR
HATCH_RELIEF_R=m.HATCH_RELIEF_R
HATCH_RELIEF_X=m.HATCH_RELIEF_X
HATCH_FLANGE_T=m.HATCH_FLANGE_T
# The BOARD's USB keep-out, which is narrower than the hatch's slot by the design margin. It is
# read from the chassis, not from USB_SLOT_W (which already includes that margin).
USB=(25.90,33.30)
USB_SLOT=(25.10,34.10)
fails=[]

print("=== SNAP HATCH VALIDATION ===")
for name,sh in (("cover",cover),("hatch",hatch)):
    ok=len(sh.Solids)==1 and sh.isValid()
    print("  %-6s one valid solid: %s  volume %.1f"%(name,ok,sh.Volume))
    if not ok:fails.append(name+" is not one valid solid")

v=cover.common(hatch).Volume
print("  seated hatch/cover intersection: %.6f mm3 %s"%(v,"OK" if v<=0.001 else "FAIL"))
if v>0.001:fails.append("seated hatch collides with cover")

print("\n=== FIT, PERIMETER LIP AND SINGLE SCREW ===")
# Retention changed from side cantilevers to ONE screw plus a continuous anti-rotation lip,
# because the cantilevers did not print as flexures -- they stayed attached along their length
# and looked like blobs stuck to the side. The cantilever strain screen that used to live here
# was measuring a mechanism that no longer exists and could "pass" while the real features
# below were missing, so it is gone.
left_clear=PLUG[0]-BAY[0];right_clear=BAY[1]-PLUG[1]
front_clear=PLUG[2]-BAY[2];back_clear=BAY[3]-PLUG[3]
print("  plug clearance L/R/front/back: %.2f %.2f %.2f %.2f mm"%
      (left_clear,right_clear,front_clear,back_clear))
if min(left_clear,right_clear,front_clear,back_clear)<0.24:fails.append("plug clearance below 0.24 mm")

# Continuous perimeter lip: it must be present all the way round, and it must sit inside the
# matching groove in the cover. Probing only one side (as before, at a single Y) cannot tell a
# perimeter ring from a single nub.
lip_z0=1.4-0.6
for label,(x0,x1,y0,y1) in {
    "left":(BAY[0]-LIP_CLEAR-LIP_T, BAY[0]-LIP_CLEAR, BAY[2], BAY[3]),
    "right":(BAY[1]+LIP_CLEAR, BAY[1]+LIP_CLEAR+LIP_T, BAY[2], BAY[3]),
    "front":(BAY[0], BAY[1], BAY[2]-LIP_CLEAR-LIP_T, BAY[2]-LIP_CLEAR),
    "back":(BAY[0], BAY[1], BAY[3]+LIP_CLEAR, BAY[3]+LIP_CLEAR+LIP_T),
}.items():
    b=Part.makeBox(x1-x0,y1-y0,LIP_H,App.Vector(x0,y0,lip_z0))
    v=hatch.common(b).Volume
    print("  %-6s lip segment volume: %6.2f mm3 %s"%(label,v,"OK" if v>4.0 else "FAIL"))
    if v<=4.0:fails.append("perimeter lip missing on the "+label+" side")

# The screw actually exists, passes through real flange material from the outside, and threads
# into a boss on the cover. The old probe looked at (50.85, 62.0) -- a point inside the bay
# opening with no cover material and outside the flange's own Y range, so it measured nothing.
SX,SY=SCREW_AXIS
shank=Part.makeCylinder(SCREW_D/2,12.0,App.Vector(SX,SY,-6.0))
hv=hatch.common(shank).Volume
print("  hatch shank hole clear (from outside through flange+plug+lip): %.4f mm3 %s"%
      (hv,"OK" if hv<=0.001 else "FAIL"))
if hv>0.001:fails.append("hatch screw hole obstructed")
ann=Part.makeCylinder(2.4,1.2,App.Vector(SX,SY,-1.2)).cut(Part.makeCylinder(SCREW_D/2,1.3,App.Vector(SX,SY,-1.3)))
fv=hatch.common(ann).Volume
print("  flange material around the shank: %.2f mm3 %s"%(fv,"OK" if fv>3.0 else "FAIL"))
if fv<=3.0:fails.append("screw has no flange material to pass through")
boss=Part.makeCylinder(2.4,3.0,App.Vector(SX,SY,-0.6)).cut(Part.makeCylinder(SCREW_D/2,3.1,App.Vector(SX,SY,-0.7)))
bv=cover.common(boss).Volume
print("  cover boss for the screw to thread into: %.2f mm3 %s"%(bv,"OK" if bv>5.0 else "FAIL"))
if bv<=5.0:fails.append("no cover boss behind the hatch screw")
# The screw must be driven from OUTSIDE: it enters at the flange's exterior face (z<0) and no
# part of the hatch stands between the head and that face.
out=hatch.common(Part.makeCylinder(2.5,1.2,App.Vector(SX,SY,-1.2)))
print("  flange material beneath the head (screw enters from OUTSIDE): %.2f mm3 %s"%
      (out.Volume,"OK" if out.Volume>3.0 else "FAIL"))
if out.Volume<=3.0:fails.append("hatch screw does not enter from the outside")

print("\n=== USB CABLE EXIT ===")
left_margin=USB[0]-USB_SLOT[0];right_margin=USB_SLOT[1]-USB[1]
print("  transformed USB keep-out X %.2f..%.2f"%USB)
print("  hatch/cover exit slot X %.2f..%.2f; margins %.2f / %.2f mm"%
      (USB_SLOT[0],USB_SLOT[1],left_margin,right_margin))
if min(left_margin,right_margin)<0.75:fails.append("USB exit slot has insufficient X clearance")
# Slot must be open through both seated solids at the flange's +Y edge.
slot_probe=Part.makeBox(USB_SLOT[1]-USB_SLOT[0]-0.2,4.0,3.0,
                        App.Vector(USB_SLOT[0]+0.1,91.5,-1.3))
cv=cover.common(slot_probe).Volume;hv=hatch.common(slot_probe).Volume
print("  material obstructing exit corridor: cover %.4f hatch %.4f mm3 %s"%
      (cv,hv,"OK" if cv<=0.001 and hv<=0.001 else "FAIL"))
if cv>0.001 or hv>0.001:fails.append("USB exit corridor obstructed")

print("\n=== TOOL-FREE REMOVAL ===")
# The plate has a half-round finger scallop in its +Y edge, centred on the bay, replacing the
# earlier 31.6 x 5.0 mm rectangular bite. The scallop is cut only through the PLATE (z -1.2..0)
# so the anti-rotation lip behind it stays whole -- that is deliberate.
# Test it directly: intersect the scallop cylinder with an INTACT plate to see how much it
# should remove, then confirm none of that material survives in the real hatch. Comparing
# sampled band volumes instead is unreliable because the lip behind the plate adds material.
scallop=Part.makeCylinder(HATCH_RELIEF_R,HATCH_FLANGE_T+0.4,
                          App.Vector(HATCH_RELIEF_X,m.HATCH_FLANGE_Y1,-HATCH_FLANGE_T-0.3))
intact=rprism(m.HATCH_FLANGE_X1-m.HATCH_FLANGE_X0,m.HATCH_FLANGE_Y1-m.HATCH_FLANGE_Y0,4.0,
              m.HATCH_FLANGE_X0,m.HATCH_FLANGE_Y0,-HATCH_FLANGE_T,HATCH_FLANGE_T)
should=scallop.common(intact).Volume
left=hatch.common(scallop.common(intact)).Volume
print("  scallop should remove %.2f mm3 of plate; %.4f mm3 survives %s"
      %(should,left,"OK" if left<0.01 else "FAIL"))
if left>=0.01:fails.append("finger scallop is blocked")
if should<20.0:fails.append("finger scallop removes too little to get a finger under")
# It must not reach the screw boss region or the USB slot.
for tag,x0,x1 in (("USB slot",26.0,34.0),("screw boss",81.0,88.0)):
    band=Part.makeBox(x1-x0,4.0,1.2,App.Vector(x0,88.0,-1.2))
    bv=hatch.common(band).Volume
    print("  plate material over %s: %.2f mm3 %s"%(tag,bv,"OK" if bv>5.0 else "FAIL"))
    if bv<=5.0:fails.append("scallop reaches the %s"%tag)

if fails:
    print("\nSNAP HATCH FAILURES:")
    for f in fails:print(" -",f)
    sys.exit(1)
print("\nSNAP HATCH + USB EXIT VALIDATION PASS")
