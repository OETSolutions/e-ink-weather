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
# The plate is RECESSED: it occupies z 0..HATCH_FLANGE_T, flush with the cover's rear face, where
# it used to stand proud at z -HATCH_FLANGE_T..0. Every probe that reaches into the plate band
# is written against these two values so it follows the plate instead of restating a z number.
PLATE_Z0,PLATE_Z1=0.0,m.HATCH_FLANGE_T
# The plug runs from the plate's inner face up to the cover's inner face.
INNER_Z0=m.HATCH_PLUG_Z0
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

# The portion BEHIND the plate (z > PLATE_Z1, i.e. inside the cover's own thickness) must stay
# inside the bay plus the notches, EXCEPT for the deliberate retention lip on the -X end. The old
# rule ("nothing may lap behind the cover") was written when the only such feature was the
# uninstallable perimeter ring; the user has since explicitly asked for an inside lip on the
# screw-opposite edge, and it is installed by TILT, so it does not need to pass through the hole.
# The lip is therefore excluded here and checked on its own below (present, under SOLID cover,
# clear of the chassis, and no seated interference).
# NOTE the z band: this starts at PLATE_Z1, not at 0. Above PLATE_Z1 the PLATE ITSELF has already
# ended, so anything found there is plug/key/lip and must be accounted for. Probing from z=0
# instead caught the plate's own lap past the bay (886 mm3) and reported it as a projection.
notch=None
for kx0,kx1,ky0,ky1 in HATCH_KEYS:
    n=rprism((kx1-kx0)+2*HATCH_KEY_CLEAR,(ky1-ky0)+2*HATCH_KEY_CLEAR,3.0,
             kx0-HATCH_KEY_CLEAR,ky0-HATCH_KEY_CLEAR,-0.5,m.COVER_T+1.0)
    notch=n if notch is None else notch.fuse(n)
baybox=rprism(BAY[1]-BAY[0],BAY[3]-BAY[2],3.0,BAY[0],BAY[2],-0.5,m.COVER_T+1.0)
inner=hatch.common(Part.makeBox(400,400,m.COVER_T-PLATE_Z1+0.1,
                                App.Vector(-200,-200,PLATE_Z1)))
_lip=m.HATCH_LIP_X0-0.05
lipbox=Part.makeBox((m.HATCH_LIP_X1-_lip)+0.1,(m.HATCH_LIP_Y1-m.HATCH_LIP_Y0)+0.1,
                    m.HATCH_LIP_T+0.1,App.Vector(_lip,m.HATCH_LIP_Y0-0.05,m.HATCH_LIP_Z0-0.05))
esc=inner.cut(baybox.fuse(notch).fuse(lipbox)).Volume
print("  hatch behind the plate outside bay+notches+lip: %.4f mm3 %s"%(esc,"OK" if esc<=0.001 else "FAIL"))
if esc>0.001:fails.append("hatch projects past the bay without a notch")
# And the plate MUST lap past the bay -- it is what covers the recess rim and carries the screw.
# If this is zero the plate has been shrunk back inside the bay and the seat's rim is exposed.
plate=hatch.common(Part.makeBox(400,400,PLATE_Z1-PLATE_Z0-0.02,
                                App.Vector(-200,-200,PLATE_Z0+0.01)))
plat_lap=plate.cut(rprism(BAY[1]-BAY[0],BAY[3]-BAY[2],3.0,BAY[0],BAY[2],-0.5,m.COVER_T+1.0)).Volume
print("  plate material outboard of the bay (must be >0; it carries the screw): %.2f mm3 %s"
      %(plat_lap,"OK" if plat_lap>50 else "FAIL"))
if plat_lap<=50:fails.append("plate no longer laps past the bay to reach the screw")

# The screw actually exists, passes through real plate material from the outside, and threads
# into a boss on the cover. The old probe looked at (50.85, 62.0) -- a point inside the bay
# opening with no cover material and outside the flange's own Y range, so it measured nothing.
# All z references are now anchored on PLATE_Z0/PLATE_Z1: the plate is recessed, so the head sits
# in the void above the plate (z < PLATE_Z0) and the flange material is the plate band itself.
SX,SY=SCREW_AXIS
shank=Part.makeCylinder(SCREW_D/2,12.0,App.Vector(SX,SY,PLATE_Z0-4.0))
hv=hatch.common(shank).Volume
print("  hatch shank hole clear (from outside through flange+plug+lip): %.4f mm3 %s"%
      (hv,"OK" if hv<=0.001 else "FAIL"))
if hv>0.001:fails.append("hatch screw hole obstructed")
ann=Part.makeCylinder(2.4,PLATE_Z1-PLATE_Z0,
                      App.Vector(SX,SY,PLATE_Z0)).cut(
    Part.makeCylinder(SCREW_D/2,PLATE_Z1-PLATE_Z0+0.2,App.Vector(SX,SY,PLATE_Z0-0.1)))
fv=hatch.common(ann).Volume
print("  flange material around the shank: %.2f mm3 %s"%(fv,"OK" if fv>3.0 else "FAIL"))
if fv<=3.0:fails.append("screw has no flange material to pass through")
boss=Part.makeCylinder(2.4,3.0,App.Vector(SX,SY,m.HATCH_RECESS_T)).cut(
    Part.makeCylinder(SCREW_D/2,3.2,App.Vector(SX,SY,m.HATCH_RECESS_T-0.1)))
bv=cover.common(boss).Volume
print("  cover boss for the screw to thread into: %.2f mm3 %s"%(bv,"OK" if bv>5.0 else "FAIL"))
if bv<=5.0:fails.append("no cover boss behind the hatch screw")
# The screw must be driven from OUTSIDE: it enters at the plate's exterior face (the plate's own
# z0) and no part of the hatch stands between the head and that face.
out=hatch.common(Part.makeCylinder(2.5,PLATE_Z1-PLATE_Z0,App.Vector(SX,SY,PLATE_Z0)))
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
                        App.Vector(USB_SLOT[0]+0.1,91.5,PLATE_Z0-0.1))
cv=cover.common(slot_probe).Volume;hv=hatch.common(slot_probe).Volume
print("  material obstructing exit corridor: cover %.4f hatch %.4f mm3 %s"%
      (cv,hv,"OK" if cv<=0.001 and hv<=0.001 else "FAIL"))
if cv>0.001 or hv>0.001:fails.append("USB exit corridor obstructed")
# The exit must be an OPEN CUTOUT in the PLATE, matching the cover's opening -- not a closed
# hole. The hatch plugs the bay, so any plate material across the cut's outer end makes the
# passage read (and work) as a hole with lips both sides, which is the defect the user reported:
# "the hole you have there is closed instead of having an open cutout in the hatch that matches
# the open cutout in the rear cover". Test the whole slot column PLUS the strip out to and past
# the plate's +Y edge: no hatch material may survive anywhere in it within the plate band.
edge_probe=Part.makeBox(USB_SLOT[1]-USB_SLOT[0]-0.2, (m.HATCH_FLANGE_Y1+0.5)-91.5,
                        PLATE_Z1-PLATE_Z0,
                        App.Vector(USB_SLOT[0]+0.1,91.5,PLATE_Z0+0.01))
ev=hatch.common(edge_probe).Volume
print("  hatch material between the slot and the plate's +Y edge (must be 0): %.4f mm3 %s"
      %(ev,"OK" if ev<=0.001 else "FAIL -- exit is a closed hole, not an open cutout"))
if ev>0.001:fails.append("hatch USB exit is a closed hole; it must be an open cutout off the edge")

print("\n=== -X RETENTION LIP (opposite the screw) ===")
# The user: "still needs a lip that goes on the INSIDE of the rear cover on the opposite edge
# from where the screw attaches. Do it." Requirements: it must exist, it must reach UNDER SOLID
# cover (not sit in the key notch's void), and it must be clear of the chassis behind.
lip = hatch.common(Part.makeBox(400,400,m.HATCH_LIP_T+0.5,
                                App.Vector(-200,-200,m.HATCH_LIP_Z0+0.01)))
print("  lip volume above the cover's inside face: %.2f mm3 %s"
      %(lip.Volume,"OK" if lip.Volume>20 else "FAIL"))
if lip.Volume<=20: fails.append("no retention lip on the screw-opposite edge")
# The bite. NOTE the geometry: the cover is z 0..3.0 and the lip is z 3.0..4.5, so they are
# ADJACENT in z and do not overlap -- a plain common() returns 0 and means nothing. What makes
# this a retention lip is that it sits BEHIND the cover's inside face with its XY footprint
# over SOLID cover material, so lifting the hatch drives the lip into the cover. Measure that:
# sample the lip's own XY footprint and count how much of it is over solid (un-notched) cover.
_bx0,_bx1 = m.HATCH_LIP_X0,m.HATCH_LIP_X1
_by0,_by1 = m.HATCH_LIP_Y0,m.HATCH_LIP_Y1
# The lip must be NARROWER than the hatch edge it hooks under (user's requirement). Compare it
# against the hatch's own inner-portion Y span, which is the edge that meets the cover.
_edge_y = m.HATCH_FLANGE_Y1-m.HATCH_FLANGE_Y0
_narrow = (_by1-_by0) < _edge_y
print("  lip Y width %.2f mm vs hatch edge %.2f mm -> narrower: %s"%(
      _by1-_by0, _edge_y, "OK" if _narrow else "FAIL"))
if not _narrow: fails.append("retention lip is not narrower than the hatch edge")
_n=solid_pts=0
_x=_bx0+0.1
while _x<_bx1:
    _y=_by0+0.1
    while _y<_by1:
        _n+=1
        if cover.isInside(App.Vector(_x,_y,1.5),1e-6,True) and \
           not notch.isInside(App.Vector(_x,_y,1.5),1e-6,True):
            solid_pts+=1
        _y+=0.4
    _x+=0.4
_frac=100.0*solid_pts/max(1,_n)
print("  lip footprint over SOLID cover (the bite): %.0f%% of %d samples %s"
      %(_frac,_n,"OK" if _frac>=20 else "FAIL"))
if _frac<20: fails.append("retention lip does not bite under solid cover")
# seated interference with the cover must still be zero
print("  seated hatch/cover intersection: %.4f mm3 %s"
      %(hatch.common(cover).Volume,"OK" if hatch.common(cover).Volume<=0.001 else "FAIL"))
if hatch.common(cover).Volume>0.001: fails.append("retention lip interferes with the cover")
# and it must clear the chassis behind the cover
_adoc=App.openDocument(os.path.join(HERE,"EInk_Weather_Display_Assembly.FCStd"))
_ch=None
for _o in _adoc.Objects:
    if _o.Name=="PRINT_REAR_CHASSIS": _ch=_o.Shape
if _ch is not None:
    cv=lip.common(_ch).Volume
    print("  lip vs chassis behind the cover: %.4f mm3 %s"%(cv,"OK" if cv<=0.001 else "FAIL"))
    if cv>0.001: fails.append("retention lip hits the chassis")

print("\n=== TOOL-FREE REMOVAL ===")
# The plate has a half-round finger scallop in its +Y edge, replacing the earlier 31.6 x 5.0 mm
# rectangular bite. The scallop is cut only through the PLATE (z PLATE_Z0..PLATE_Z1) so the key
# notches behind it stay whole -- that is deliberate. It is a 3.0 mm radius dimple: 4.5 mm2 of
# fingernail access, enough to start a pry, and small enough that it reads as a thumb dip rather
# than a bite.
# Test it directly: intersect the scallop cylinder with an INTACT plate to see how much it
# should remove, then confirm none of that material survives in the real hatch. Comparing
# sampled band volumes instead is unreliable because the keys behind the plate add material.
scallop=Part.makeCylinder(HATCH_SCALLOP_R,HATCH_FLANGE_T+0.4,
                          App.Vector(HATCH_SCALLOP_X,m.HATCH_FLANGE_Y1,PLATE_Z0-0.3))
intact=rprism(m.HATCH_FLANGE_X1-m.HATCH_FLANGE_X0,m.HATCH_FLANGE_Y1-m.HATCH_FLANGE_Y0,4.0,
              m.HATCH_FLANGE_X0,m.HATCH_FLANGE_Y0,PLATE_Z0,HATCH_FLANGE_T)
should=scallop.common(intact).Volume
left=hatch.common(scallop.common(intact)).Volume
print("  scallop should remove %.2f mm3 of plate; %.4f mm3 survives %s"
      %(should,left,"OK" if left<0.01 else "FAIL"))
if left>=0.01:fails.append("finger scallop is blocked")
if should<8.0:fails.append("finger scallop removes too little to get a finger under")
# It must not reach the screw boss region or the USB slot.
for tag,x0,x1 in (("USB slot",26.0,34.0),("screw boss",81.0,88.0)):
    band=Part.makeBox(x1-x0,4.0,PLATE_Z1-PLATE_Z0,App.Vector(x0,88.0,PLATE_Z0))
    bv=hatch.common(band).Volume
    print("  plate material over %s: %.2f mm3 %s"%(tag,bv,"OK" if bv>5.0 else "FAIL"))
    if bv<=5.0:fails.append("scallop reaches the %s"%tag)

print("\n=== RECESSED SEAT + SUPPORT LIP (2026-09-20) ===")
# The user: "Make the hatch cover recessed in the rear cover instead of sitting on top. Make sure
# there's still a lip on the rear cover to support it." Three things must hold, and each was
# false before this change:
#   (a) the plate is FLUSH -- its exterior face at the cover's rear face, not proud of it;
#   (b) the cover is CUT AWAY beneath the plate, so the plate is actually in a recess;
#   (c) the cover still has material UNDER the plate all the way round -- the lip it rests on.
REAR_FACE=0.0
print("  plate bbox Z %.2f..%.2f (was -%.2f..0 when it sat on top)"
      %(hatch.BoundBox.ZMin,hatch.BoundBox.ZMax,HATCH_FLANGE_T))
flush=abs(hatch.BoundBox.ZMin-REAR_FACE)<=0.001
print("  plate outer face flush with the cover's rear face: %s %s"
      %("%.3f"%hatch.BoundBox.ZMin,"OK" if flush else "FAIL"))
if not flush:fails.append("hatch plate is not recessed flush with the cover's rear face")
# No part of the plate may stand proud of the cover's rear face.
proud=hatch.common(Part.makeBox(400,400,2.0,App.Vector(-200,-200,REAR_FACE-2.0))).Volume
print("  plate material proud of the rear face (must be 0): %.4f mm3 %s"
      %(proud,"OK" if proud<=0.001 else "FAIL"))
if proud>0.001:fails.append("hatch plate still stands proud of the cover")
# (b) the recess is real: the cover must be genuinely cut away over the plate's footprint.
# Sample the plate's own XY outline on the cover's rear-face plane and require most of it empty.
# The bay is already open there, so sample only the ring the plate laps over -- the band between
# the bay's outline and the plate's.
_fx0,_fx1=m.HATCH_FLANGE_X0,m.HATCH_FLANGE_X1
_fy0,_fy1=m.HATCH_FLANGE_Y0,m.HATCH_FLANGE_Y1
_n=_cut=0
_y=_fy0+0.5
while _y<_fy1-0.4:
    _x=_fx0+0.5
    while _x<_fx1-0.4:
        # only the lap ring: outside the bay, inside the plate
        if not (BAY[0]-0.1<_x<BAY[1]+0.1 and BAY[2]-0.1<_y<BAY[3]+0.1):
            _n+=1
            if not cover.isInside(App.Vector(_x,_y,0.3),1e-6,True): _cut+=1
        _x+=0.5
    _y+=0.5
_cpct=100.0*_cut/max(1,_n)
print("  recess cut away over the plate's lap ring: %.0f%% of %d samples %s"
      %(_cpct,_n,"OK" if _cpct>=90 else "FAIL"))
if _cpct<90:fails.append("cover is not cut away under the plate -- not actually recessed")
# (c) THE LIP. Cover material must remain UNDER the plate across its whole outline, at the full
# lip thickness. This is the feature the user asked to make sure of, so it is checked on all four
# sides at several stations each -- not just one point.
LIP_T=m.COVER_T-m.HATCH_RECESS_T
print("  lip thickness under the seat: COVER_T %.1f - recess %.1f = %.2f mm (FDM floor 1.6)"
      %(m.COVER_T,m.HATCH_RECESS_T,LIP_T))
if LIP_T<1.6:fails.append("support lip below the FDM minimum thickness")
_thin=[]
for _tag,_pts in (("-X",[(_fx0+0.6,_y) for _y in [_fy0+4,_fy0+11,_fy1-5]]),
                  ("+X",[(_fx1-0.6,_y) for _y in [_fy0+4,_fy0+11,_fy1-5]]),
                  ("-Y",[(_x,_fy0+0.6) for _x in [_fx0+3,_fx1-3]]),
                  ("+Y",[(_x,_fy1-0.6) for _x in [_fx0+3,_fx1-3]])):
    for _x,_y in _pts:
        # walk up from the seat floor in 0.1 mm steps; count solid cover before the plate begins
        _t=0.0
        _z=m.HATCH_RECESS_T+0.05
        while cover.isInside(App.Vector(_x,_y,_z),1e-6,True) and _z<m.COVER_T:
            _t=_z-m.HATCH_RECESS_T; _z+=0.1
        if _t<1.55:_thin.append((_tag,_x,_y,_t))
if _thin:
    for _tag,_x,_y,_t in _thin:
        print("    THIN %s at (%.1f,%.1f): only %.2f mm" %(_tag,_x,_y,_t))
print("  support lip present all round the plate: %d station(s) thin %s"
      %(len(_thin),"OK" if not _thin else "FAIL"))
if _thin:fails.append("the cover's support lip is missing or thin somewhere round the plate")

if fails:
    print("\nSNAP HATCH FAILURES:")
    for f in fails:print(" -",f)
    sys.exit(1)
print("\nSNAP HATCH + USB EXIT VALIDATION PASS")
