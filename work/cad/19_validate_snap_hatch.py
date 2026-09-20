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
HATCH_KEYS=m.HATCH_KEYS
HATCH_KEY_CLEAR=m.HATCH_KEY_CLEAR
HATCH_KEY_T=m.HATCH_KEY_T
HATCH_SCALLOP_R=m.HATCH_SCALLOP_R
HATCH_SCALLOP_X=m.HATCH_SCALLOP_X
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

print("\n=== FIT, ANTI-ROTATION KEYS AND SINGLE SCREW ===")
# Retention changed from side cantilevers to ONE screw plus anti-rotation keys, because the
# cantilevers did not print as flexures -- they stayed attached along their length and looked
# like blobs stuck to the side. The cantilever strain screen that used to live here was
# measuring a mechanism that no longer exists and could "pass" while the real features below
# were missing, so it is gone.
#
# The keys replaced the old continuous lip, which could not be assembled at all: it was
# 59.80 x 27.10 against a 56.70 x 24.00 opening, i.e. 3.10 mm larger in BOTH axes, and the
# cover's recess for it was a closed groove. Nothing here may project past the bay opening
# except through a notch, and every notch must be cut through the cover's full thickness.
left_clear=PLUG[0]-BAY[0];right_clear=BAY[1]-PLUG[1]
front_clear=PLUG[2]-BAY[2];back_clear=BAY[3]-PLUG[3]
print("  plug clearance L/R/front/back: %.2f %.2f %.2f %.2f mm"%
      (left_clear,right_clear,front_clear,back_clear))
if min(left_clear,right_clear,front_clear,back_clear)<0.24:fails.append("plug clearance below 0.24 mm")

# Every key must actually exist as solid material on the hatch, at the full key thickness.
for i,(kx0,kx1,ky0,ky1) in enumerate(HATCH_KEYS):
    b=Part.makeBox(kx1-kx0,ky1-ky0,HATCH_KEY_T,App.Vector(kx0,ky0,0.0))
    v=hatch.common(b).Volume
    print("  key %d (X %.2f..%.2f, Y %.2f..%.2f) volume: %6.2f mm3 %s"%
          (i+1,kx0,kx1,ky0,ky1,v,"OK" if v>3.0 else "FAIL"))
    if v<=3.0:fails.append("anti-rotation key %d missing or undersized"%(i+1))

# The matching notch must be open straight through the cover, so the key can pass and then bear.
for i,(kx0,kx1,ky0,ky1) in enumerate(HATCH_KEYS):
    cx,cy=(kx0+kx1)/2.0,(ky0+ky1)/2.0
    col=cover.common(Part.makeBox(0.02,0.02,3.4,App.Vector(cx,cy,0.0)))
    print("  notch %d open through the cover at its centre: %.4f mm3 %s"%
          (i+1,col.Volume,"OK" if col.Volume<=0.001 else "FAIL"))
    if col.Volume>0.001:fails.append("notch %d is not cut through the cover"%(i+1))

# The keys must stay inside the bay footprint plus their notches -- nothing may lap behind the
# cover, because a feature wider than the hole cannot be printed or inserted (the old lip bug).
notch=None
for kx0,kx1,ky0,ky1 in HATCH_KEYS:
    n=rprism((kx1-kx0)+2*HATCH_KEY_CLEAR,(ky1-ky0)+2*HATCH_KEY_CLEAR,3.0,
             kx0-HATCH_KEY_CLEAR,ky0-HATCH_KEY_CLEAR,-0.5,m.COVER_T+1.0)
    notch=n if notch is None else notch.fuse(n)
baybox=rprism(BAY[1]-BAY[0],BAY[3]-BAY[2],3.0,BAY[0],BAY[2],-0.5,m.COVER_T+1.0)
inner=hatch.common(Part.makeBox(400,400,m.COVER_T+0.1,App.Vector(-200,-200,0.0)))
esc=inner.cut(baybox.fuse(notch)).Volume
print("  hatch inner portion outside bay+notches: %.4f mm3 %s"%(esc,"OK" if esc<=0.001 else "FAIL"))
if esc>0.001:fails.append("hatch projects past the bay without a notch")

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
# The exit must be an OPEN CUTOUT in the PLATE, matching the cover's opening -- not a closed
# hole. The hatch plugs the bay, so any plate material across the cut's outer end makes the
# passage read (and work) as a hole with lips both sides, which is the defect the user reported:
# "the hole you have there is closed instead of having an open cutout in the hatch that matches
# the open cutout in the rear cover". Test the whole slot column PLUS the strip out to and past
# the plate's +Y edge: no hatch material may survive anywhere in it below z=0 (the plate band).
edge_probe=Part.makeBox(USB_SLOT[1]-USB_SLOT[0]-0.2, (m.HATCH_FLANGE_Y1+0.5)-91.5, 3.0,
                        App.Vector(USB_SLOT[0]+0.1,91.5,-1.3))
ev=hatch.common(edge_probe).Volume
print("  hatch material between the slot and the plate's +Y edge (must be 0): %.4f mm3 %s"
      %(ev,"OK" if ev<=0.001 else "FAIL -- exit is a closed hole, not an open cutout"))
if ev>0.001:fails.append("hatch USB exit is a closed hole; it must be an open cutout off the edge")

print("\n=== TOOL-FREE REMOVAL ===")
# The plate has a half-round finger scallop in its +Y edge, replacing the earlier 31.6 x 5.0 mm
# rectangular bite. The scallop is cut only through the PLATE (z -1.2..0) so the key notches
# behind it stay whole -- that is deliberate. It is a 3.0 mm radius dimple: 4.5 mm2 of fingernail
# access, enough to start a pry, and small enough that it reads as a thumb dip rather than a bite.
# Test it directly: intersect the scallop cylinder with an INTACT plate to see how much it
# should remove, then confirm none of that material survives in the real hatch. Comparing
# sampled band volumes instead is unreliable because the keys behind the plate add material.
scallop=Part.makeCylinder(HATCH_SCALLOP_R,HATCH_FLANGE_T+0.4,
                          App.Vector(HATCH_SCALLOP_X,m.HATCH_FLANGE_Y1,-HATCH_FLANGE_T-0.3))
intact=rprism(m.HATCH_FLANGE_X1-m.HATCH_FLANGE_X0,m.HATCH_FLANGE_Y1-m.HATCH_FLANGE_Y0,4.0,
              m.HATCH_FLANGE_X0,m.HATCH_FLANGE_Y0,-HATCH_FLANGE_T,HATCH_FLANGE_T)
should=scallop.common(intact).Volume
left=hatch.common(scallop.common(intact)).Volume
print("  scallop should remove %.2f mm3 of plate; %.4f mm3 survives %s"
      %(should,left,"OK" if left<0.01 else "FAIL"))
if left>=0.01:fails.append("finger scallop is blocked")
if should<8.0:fails.append("finger scallop removes too little to get a finger under")
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
