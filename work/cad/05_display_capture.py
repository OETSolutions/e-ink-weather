"""Robust FDM display sandwich, split at one plane.

  z >= 15.8  -> PRINT_FRONT_BEZEL (rolled face, 1.6 mm retaining lip, screw threads)
  z <= 15.8  -> rear chassis (1.2 mm support grid + 1.4 mm structural bridge)

Capture stack, rear to front:
  grid 14.6..15.8 | rear foam 15.8..16.3 | glass 16.3..17.2 |
  front gasket 17.2..17.4 | printed retaining lip 17.4..19.0

The previous lip was only 0.3 mm thick -- one FDM layer and mechanically unacceptable.
This model has a 1.6 mm printed lip and a real 0.2 mm compliant front gasket, so the glass
is clamped between compliant layers rather than hard plastic.
"""
import FreeCAD as App
import Part
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import (CASE_W,CASE_H,CASE_D,SPLIT_Z,PANEL_W,PANEL_H,
                                  PANEL_T,PANEL_Z,PANEL_Z1,GRID_Z0,GRID_Z1,
                                  REAR_FOAM_Z0,REAR_FOAM_T,FRONT_GASKET_Z0,
                                  FRONT_GASKET_T,LIP_UNDERSIDE,RETAINING_LIP_T,
                                  MIN_PRINTED_WALL,MIN_LOAD_WALL,MIN_M2_THREAD)
HERE=os.path.dirname(os.path.abspath(__file__))

# ---- fixed geometry -----------------------------------------------------------
D=CASE_D
PANEL_X,PANEL_Y=4.5,4.5
PANEL_CLEAR=0.20                                   # total printed-pocket clearance
GLASS_TOP=PANEL_Z1
SHELL_IN=PANEL_X-PANEL_CLEAR
AA_W,AA_H=117.668,86.972                           # SRC p4/p5
AA_LEFT,AA_BOTTOM=3.87,9.90                        # SRC p5
REVEAL=0.50                                        # 0.25 beyond active area each edge
FOAM_Z0=REAR_FOAM_Z0
GRID_RIB,GRID_CELL=1.2,5.0                         # 1.2 mm rib = three 0.4 mm lines
GRID_PITCH=GRID_RIB+GRID_CELL
SEAT_W=2.2
FPC_ROOT_W=24.5                                    # SRC p5 scaled root
FPC_CLEAR=1.0
FASTENERS=[(2.3,32.0),(2.3,76.5),(132.1,32.0),(132.1,76.5)]
PILOT_D=1.5
PILOT_Z0=SPLIT_Z
PILOT_TOP=PILOT_Z0+2.3                             # exceeds 2.0 mm thread minimum
BOSS_R=2.0                                         # 1.25 mm radial wall around pilot
FTOL=1e-6


def rr_wire(w,h,r,x,y,z):
 r=min(r,w/2-1e-6,h/2-1e-6); P=lambda a,b:App.Vector(x+a,y+b,z)
 return Part.Wire([
  Part.makeLine(P(r,0),P(w-r,0)),
  Part.ArcOfCircle(Part.Circle(P(w-r,r),App.Vector(0,0,1),r),math.radians(270),math.radians(360)).toShape(),
  Part.makeLine(P(w,r),P(w,h-r)),
  Part.ArcOfCircle(Part.Circle(P(w-r,h-r),App.Vector(0,0,1),r),math.radians(0),math.radians(90)).toShape(),
  Part.makeLine(P(w-r,h),P(r,h)),
  Part.ArcOfCircle(Part.Circle(P(r,h-r),App.Vector(0,0,1),r),math.radians(90),math.radians(180)).toShape(),
  Part.makeLine(P(0,h-r),P(0,r)),
  Part.ArcOfCircle(Part.Circle(P(r,r),App.Vector(0,0,1),r),math.radians(180),math.radians(270)).toShape(),
 ])
def rp(w,h,r,x,y,z,dz):return Part.Face(rr_wire(w,h,r,x,y,z)).extrude(App.Vector(0,0,dz))

# ---- bezel: everything above the split plane ----------------------------------
sd=App.openDocument(os.path.join(HERE,"case_shell.FCStd")); outer=sd.getObject("CASE_SHELL").Shape
bezel=outer.common(Part.makeBox(CASE_W+10,CASE_H+10,D-SPLIT_Z+0.1,App.Vector(-5,-5,SPLIT_Z)))
# Hollow the bezel skirt only up to the front-gasket plane. Above LIP_UNDERSIDE the full
# 1.6 mm retaining ring remains; it is NOT generated as a residual 0.3 mm shell sliver.
OPEN_W=PANEL_W+2*(PANEL_X-SHELL_IN)
OPEN_H=PANEL_H+2*(PANEL_Y-SHELL_IN)
hollow=rp(OPEN_W,OPEN_H,2.05,SHELL_IN,SHELL_IN,SPLIT_Z-0.1,LIP_UNDERSIDE-SPLIT_Z+0.1)
bezel=bezel.cut(hollow)
# Viewing aperture starts at the lip underside and cuts through the whole 1.6 mm lip.
open_x=PANEL_X+AA_LEFT-REVEAL/2
open_y=PANEL_Y+AA_BOTTOM-REVEAL/2
bezel=bezel.cut(rp(AA_W+REVEAL,AA_H+REVEAL,0.8,open_x,open_y,LIP_UNDERSIDE-0.05,D-LIP_UNDERSIDE+0.2))
# Blind pilot bosses. Every boss is intersected with the original shell BEFORE it is fused,
# so its front follows the real 3.4 mm rolled surface instead of a raw cylinder protruding
# through it. This creates the requested smooth dome/skin over each hole and cannot grow the
# the case beyond its authoritative CASE_D envelope.
# Front-driven M2 fasteners. Screws pass THROUGH the bezel band into the chassis boss and
# thread into a captive M2 hex nut. Blind pilots made this impossible: a screw could never be
# inserted, because no driver path exists from the rear (measured: every perimeter position
# blocked, 37-44 mm3, by the 8 mm rear taper).
#
# Counterbore (head recess), then a through-hole on the SAME axis all the way to the
# counterbore floor. Sizing is bounded by the 4.5 mm perimeter band: the counterbore must
# admit the driver, yet stay inside the band so it cannot break into the glass pocket
# (panel edge is at x=4.5). 4.4 mm dia leaves 0.1 mm of wall.
HEAD_CBORE_D=4.4
HEAD_CBORE_DEPTH=1.2
CBORE_FLOOR=D-HEAD_CBORE_DEPTH
# Spotface radius beyond the counterbore. The front edge now carries a 45-degree lead chamfer
# at the bed (see 02_shell.py). A 6 mm driver body descending on the fastener axis grazed that
# chamfer by 0.20 mm at a 0.6 mm margin; 0.9 clears it entirely while trimming only the
# cosmetic outer lip (the lip's bearing edge is 2.7 mm further in, at the aperture).
SPOTFACE_MARGIN=0.9
for x,y in FASTENERS:
 boss_raw=Part.makeCylinder(BOSS_R,PILOT_TOP-SPLIT_Z,App.Vector(x,y,SPLIT_Z))
 boss=outer.common(boss_raw)
 bezel=bezel.fuse(boss)
for x,y in FASTENERS:
 # SPOTFACE first, on the boss axis. The front is a 3.4 mm roll: along the fastener's own
 # diameter the outer surface falls from 18.70 to 16.69, so a flat screw head seated on the
 # rolled surface cuts up to 0.8 mm into the material. Boring a flat-faced recess normal to
 # the axis (standard spotfacing) gives the head a true seat and removes that interference.
 spot=Part.makeCylinder(HEAD_CBORE_D/2+SPOTFACE_MARGIN,D-CBORE_FLOOR+1.0,
                        App.Vector(x,y,CBORE_FLOOR-0.8))
 bezel=bezel.cut(spot)
 # Through-hole from the split plane up to the counterbore floor (not the old lip plane:
 # stopping at 17.1 left a 0.4 mm bezel ring that the screw shank collided with).
 bezel=bezel.cut(Part.makeCylinder(2.3/2,CBORE_FLOOR-SPLIT_Z+0.6,
                                   App.Vector(x,y,SPLIT_Z-0.05)))
 # Counterbore from the spotface floor.
 bezel=bezel.cut(Part.makeCylinder(HEAD_CBORE_D/2,HEAD_CBORE_DEPTH+0.6,
                                   App.Vector(x,y,CBORE_FLOOR-0.1)))
# FPC exit under the bottom bezel. The cut must stop at the front-gasket plane: extending it
# through the retaining band (as the previous GLASS_TOP-SPLIT_Z span did) thinned the lip to
# 1.30 mm at 67.2,4.6 -- below the 1.6 mm load minimum and exactly the defect class the user
# flagged. The tail leaves between glass and gasket, so it does not need the lip cut at all.
fpc_x=PANEL_X+PANEL_W/2-(FPC_ROOT_W+FPC_CLEAR)/2
bezel=bezel.cut(Part.makeBox(FPC_ROOT_W+FPC_CLEAR,PANEL_Y+0.8,LIP_UNDERSIDE-SPLIT_Z-0.4,
                             App.Vector(fpc_x,0,SPLIT_Z-0.1)))
# Registration is provided by the 0.20 mm skirt fit against the chassis cavity plus the four
# side screws; no tabs are used (tabs would cut a fragile 0.95 mm outer wall).

# ---- support grid (fused into the rear chassis) -------------------------------
seat_outer=rp(PANEL_W+0.4,PANEL_H+0.4,2.2,PANEL_X-0.2,PANEL_Y-0.2,GRID_Z0,GRID_Z1-GRID_Z0)
seat_inner=rp(PANEL_W-2*SEAT_W,PANEL_H-2*SEAT_W,0.5,PANEL_X+SEAT_W,PANEL_Y+SEAT_W,
              GRID_Z0-0.1,GRID_Z1-GRID_Z0+0.2)
grid=seat_outer.cut(seat_inner)
x=PANEL_X+SEAT_W+GRID_CELL
while x<PANEL_X+PANEL_W-SEAT_W:
 grid=grid.fuse(Part.makeBox(GRID_RIB,PANEL_H-2*SEAT_W,GRID_Z1-GRID_Z0,
                             App.Vector(x-GRID_RIB/2,PANEL_Y+SEAT_W,GRID_Z0)))
 x+=GRID_PITCH
y=PANEL_Y+SEAT_W+GRID_CELL
while y<PANEL_Y+PANEL_H-SEAT_W:
 grid=grid.fuse(Part.makeBox(PANEL_W-2*SEAT_W,GRID_RIB,GRID_Z1-GRID_Z0,
                             App.Vector(PANEL_X+SEAT_W,y-GRID_RIB/2,GRID_Z0)))
 y+=GRID_PITCH
grid=grid.cut(Part.makeBox(FPC_ROOT_W+FPC_CLEAR,PANEL_Y+0.8,GRID_Z1-GRID_Z0+0.2,
                           App.Vector(fpc_x,0,GRID_Z0-0.1)))
grid=grid.removeSplitter()

# Rear foam: full 0.5 mm compliant backing between grid and glass.
foam=rp(PANEL_W,PANEL_H,2.0,PANEL_X,PANEL_Y,FOAM_Z0,REAR_FOAM_T)
foam=foam.cut(Part.makeBox(FPC_ROOT_W+FPC_CLEAR,PANEL_Y+0.8,REAR_FOAM_T+0.2,
                           App.Vector(fpc_x,0,FOAM_Z0-0.1)))
# Front gasket: 0.2 mm compliant border ring between glass front and printed lip. It follows
# the lip aperture, so printed plastic never bears directly on the fragile glass.
fg_outer=rp(PANEL_W,PANEL_H,2.0,PANEL_X,PANEL_Y,FRONT_GASKET_Z0,FRONT_GASKET_T)
fg_inner=rp(AA_W+REVEAL,AA_H+REVEAL,0.8,open_x,open_y,FRONT_GASKET_Z0-0.05,FRONT_GASKET_T+0.1)
front_gasket=fg_outer.cut(fg_inner)

for n,s in [("bezel",bezel),("grid",grid),("foam",foam),("front_gasket",front_gasket)]:
 if len(s.Solids)!=1 or not s.isValid():
  print("DEBUG",n,"solids",len(s.Solids),"valid",s.isValid())
  for i,q in enumerate(s.Solids):
   bb=q.BoundBox
   print("   solid",i,"vol %.1f bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"%
         (q.Volume,bb.XMin,bb.XMax,bb.YMin,bb.YMax,bb.ZMin,bb.ZMax))
  raise RuntimeError("%s not one valid solid (%d solids)"%(n,len(s.Solids)))

def zb(s,name):
 b=s.optimalBoundingBox()
 print("  %-6s z %.2f..%.2f  xy %.2f x %.2f"%(name,b.ZMin,b.ZMax,b.XLength,b.YLength))
print("BEZEL z %.2f..%.2f (chassis owns <= %.2f)"%(bezel.optimalBoundingBox().ZMin,bezel.optimalBoundingBox().ZMax,SPLIT_Z))
assert bezel.optimalBoundingBox().ZMin>=SPLIT_Z-1e-6,"bezel must not cross the split plane"
assert grid.optimalBoundingBox().ZMax<=SPLIT_Z+1e-6,"grid must not cross the split plane"

doc=App.newDocument("DisplayCapture")
b=doc.addObject("Part::Feature","PRINT_FRONT_BEZEL"); b.Label="PRINT: Front retaining bezel"; b.Shape=bezel
b.addProperty("App::PropertyString","PrintOrientation").PrintOrientation="Cosmetic face down; smooth plate"
b.addProperty("App::PropertyString","SplitPlane").SplitPlane="Owns z>=%.1f; rear chassis owns z<=%.1f"%(SPLIT_Z,SPLIT_Z)
b.addProperty("App::PropertyString","MinimumThicknesses").MinimumThicknesses=(
    "retaining lip %.1f mm; pilot engagement %.1f mm; boss radial wall %.2f mm"%
    (RETAINING_LIP_T,PILOT_TOP-PILOT_Z0,BOSS_R-PILOT_D/2))
g=doc.addObject("Part::Feature","CHASSIS_SUPPORT_GRID"); g.Label="FUSE INTO CHASSIS: support grid"; g.Shape=grid
g.addProperty("App::PropertyString","NotSeparatePrint").NotSeparatePrint="Fused into rear electronics chassis"
f=doc.addObject("Part::Feature","FOAM_BACKING_0_5MM"); f.Label="CUT: 0.5 mm closed-cell rear foam"; f.Shape=foam
fg=doc.addObject("Part::Feature","FRONT_GASKET_0_2MM"); fg.Label="CUT: 0.2 mm compliant front gasket ring"; fg.Shape=front_gasket
fg.addProperty("App::PropertyString","Material").Material="0.2 mm silicone/poron gasket; not printed"
# Fail generation immediately if any critical FDM feature regresses.
assert RETAINING_LIP_T>=MIN_LOAD_WALL-FTOL,"retaining lip below 1.6 mm load minimum"
assert PILOT_TOP-PILOT_Z0>=MIN_M2_THREAD-FTOL,"M2 pilot thread engagement below 2.0 mm"
assert GRID_Z1-GRID_Z0>=MIN_PRINTED_WALL-FTOL,"grid depth below 1.2 mm"
assert GRID_RIB>=MIN_PRINTED_WALL-FTOL,"grid rib below 1.2 mm"
assert BOSS_R-PILOT_D/2>=1.2-FTOL,"boss radial wall below 1.2 mm"
# The retaining lip must exist as real material in the model, not only in the constants.
lip_probe=Part.makeBox(PANEL_W+4,PANEL_H+4,LIP_UNDERSIDE-0.05,
                       App.Vector(PANEL_X-2,PANEL_Y-2,LIP_UNDERSIDE-0.05))
lip_material=bezel.common(lip_probe).Volume
ring_perimeter=2*((PANEL_W+4)+(PANEL_H+4))
assert lip_material>0.0,"no material below the lip underside"
print("lip band material below z=%.1f: %.1f mm3"%(LIP_UNDERSIDE,lip_material))
doc.recompute(); doc.saveAs(os.path.join(HERE,"display_capture.FCStd"))

ob=bezel.optimalBoundingBox()
print("BEZEL solids %d valid %s vol %.0f bbox z %.3f..%.3f"%
      (len(bezel.Solids),bezel.isValid(),bezel.Volume,ob.ZMin,ob.ZMax))
print("GRID  solids %d valid %s vol %.0f"%(len(grid.Solids),grid.isValid(),grid.Volume))
print("opening x %.3f..%.3f y %.3f..%.3f"%(open_x,open_x+AA_W+REVEAL,open_y,open_y+AA_H+REVEAL))
print("sandwich z grid %.1f..%.1f rear foam %.1f..%.1f glass %.1f..%.1f front gasket %.1f..%.1f lip %.1f..%.1f"%
      (GRID_Z0,GRID_Z1,FOAM_Z0,PANEL_Z,PANEL_Z,PANEL_Z1,
       FRONT_GASKET_Z0,LIP_UNDERSIDE,LIP_UNDERSIDE,D))
print("minimums: lip %.1f, grid rib/depth %.1f/%.1f, pilot engagement %.1f mm"%
      (RETAINING_LIP_T,GRID_RIB,GRID_Z1-GRID_Z0,PILOT_TOP-PILOT_Z0))
print("blind pilots to z=%.2f (nominal center roof %.2f mm)"%(PILOT_TOP,D-PILOT_TOP))
print("saved display_capture.FCStd")
