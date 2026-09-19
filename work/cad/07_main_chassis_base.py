"""Base printable main chassis: rolled shell + support grid + rear-opening side tub.

One FDM-printable body, front-face-down:
  - generated rolled front and 8 mm tapered rear sides
  - 1.2 mm panel support grid, fused through a 1.4 mm structural bridge
  - open rear above the 3.0 mm cover plane for electronics insertion
  - four real rear-cover screw bosses

Board standoffs, battery retainers, and connector service bay are added only after the
real folded-FPC assembly fixes their locations (next build stage).
"""
import FreeCAD as App
import Part
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import (CASE_W,CASE_H,CASE_D,SPLIT_Z,REAR_COVER_T,GRID_Z0,
                                  GRID_Z1,MIN_LOAD_WALL,MIN_PRINTED_WALL,
                                  MIN_M3_THREAD,M2_PILOT,M2_ENGAGE)

HERE=os.path.dirname(os.path.abspath(__file__))
D=CASE_D
COVER_INNER_Z=REAR_COVER_T                    # cover inner face
TUB_TOP=GRID_Z0+0.20                          # overlap up into the support grid
WALL=1.8                                      # PETG perimeter wall
BACK_CHAMFER=8.0                              # USER requested reduced rear footprint
CORNER_R=9.0                                  # USER rounded corners
FASTENERS=[(2.3,32.0),(2.3,76.5),(132.1,32.0),(132.1,76.5)]  # DERIVED bezel-safe
SCREW_D=2.4                                   # M2 clearance, rear-driven bezel screw
SCREW_HEAD_D=4.5                              # M2 pan head
CHANNEL_Z1=SPLIT_Z                            # head seat -> split plane
CHANNEL_Z0=CHANNEL_Z1-4.4
# Rear-cover bosses. These were documented but never built in the previous revision, so the
# cover's four screws had no load path at all. M2 (user: "M2 screws are better"): 1.6 mm pilot
# and 4.0 mm engagement, which is 2x the screw diameter -- the usual minimum for a self-tap in
# plastic. The boss radius drops with the pilot, keeping the 2.6 mm radial wall.
COVER_SCREW_XY=[(11.0,11.0),(CASE_W-11.0,11.0),(11.0,CASE_H-11.0),(CASE_W-11.0,CASE_H-11.0)]
COVER_BOSS_R=3.7                              # M2 boss: 2.9 mm wall around the 1.6 mm pilot
COVER_PILOT_D=M2_PILOT                        # 1.6 M2 self-tapping pilot
COVER_BOSS_ENGAGE=M2_ENGAGE                   # 4.0 mm = 2x diameter, the self-tap minimum
BRIDGE_T=1.4                                  # structural tub-to-grid link (>1.2 minimum)


def rounded_rect_wire(w,h,r,x,y,z):
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


def inner_wire(z):
    # Uniform 1.8 mm wall against the 45-degree rear chamfer.
    inset=max(0.0,BACK_CHAMFER-z)
    ox=inset+WALL
    w=CASE_W-2*ox; h=CASE_H-2*ox
    outer_r=max(1.0,CORNER_R-inset)
    r=max(0.5,outer_r-WALL)
    return rounded_rect_wire(w,h,r,ox,ox,z)


# Exact verified outer shell between rear-cover plane and tub/front overlap.
src=App.openDocument(os.path.join(HERE,"case_shell.FCStd"))
outer=src.getObject("CASE_SHELL").Shape
clip=Part.makeBox(CASE_W+10,CASE_H+10,TUB_TOP-COVER_INNER_Z,
                  App.Vector(-5,-5,COVER_INNER_Z))
tub_solid=outer.common(clip)

# Lofted cavity follows the 8 mm taper, then remains constant to the top.
sections=[inner_wire(COVER_INNER_Z-0.05),inner_wire(BACK_CHAMFER),inner_wire(TUB_TOP+0.1)]
cavity=Part.makeLoft(sections,True,False)
tub=tub_solid.cut(cavity)

# Fuse the support grid that backs the glass; the cosmetic retaining bezel remains a
# separate printable part so the display is sandwiched during assembly.
dc=App.openDocument(os.path.join(HERE,"display_capture.FCStd"))
grid=dc.getObject("CHASSIS_SUPPORT_GRID").Shape
# Structural bridge linking the side tub to the grid seat, under the panel border and outside
# the active area. The previous bridge was 0.35 mm thick -- barely one extrusion line, and
# not a real load path. This one is 1.4 mm and spans z GRID_Z0-1.4 .. GRID_Z0+0.2 so it
# overlaps both the tub wall and the grid seat.
bridge_outer=Part.Face(rounded_rect_wire(CASE_W-3.6+0.8,CASE_H-3.6+0.8,7.2,1.4,1.4,
                                         GRID_Z0-BRIDGE_T)).extrude(App.Vector(0,0,BRIDGE_T+0.2))
bridge_inner=Part.Face(rounded_rect_wire(124.8,98.9,1.7,4.8,4.8,
                                         GRID_Z0-BRIDGE_T-0.05)).extrude(App.Vector(0,0,BRIDGE_T+0.3))
bridge=bridge_outer.cut(bridge_inner)
# FPC passage through the bridge. The folded tail dips to y ~0.9..1.9 in this z band (its
# 180-degree fold passes below the panel edge), so the full perimeter ring must be notched or
# the thicker bridge now clips the tail by ~7 mm3. Same corridor the grid uses, extended down
# through the bridge's whole thickness.
fpc_gap_x0=4.5+125.4/2-(24.5+1.0)/2
fpc_gap=Part.makeBox(24.5+1.0,5.3,(GRID_Z1-GRID_Z0)+2.0,
                     App.Vector(fpc_gap_x0,0.0,GRID_Z0-BRIDGE_T-0.4))
bridge=bridge.cut(fpc_gap)
print("tub-grid intersection",tub.common(grid).Volume,
      "bridge tub",bridge.common(tub).Volume,"bridge grid",bridge.common(grid).Volume)
chassis=tub.fuse(bridge).fuse(grid).removeSplitter()

# ---- rear-cover bosses ---------------------------------------------------------
# The four cover screws previously had NO load path: the header claimed bosses but none were
# generated, so the cover was attached to nothing. Each boss now rises from the cover plane
# with 4.0 mm of usable self-tapping engagement AND is tied back to the adjacent side walls
# by two printed gussets. The gussets are required, not decorative: at the cover plane the
# tapered wall inner face is only ~6.8 mm from the corner, so a boss at (11,11) would
# otherwise sit in open cavity and carry no load at all.
for x,y in COVER_SCREW_XY:
    boss=Part.makeCylinder(COVER_BOSS_R,COVER_BOSS_ENGAGE+1.2,
                           App.Vector(x,y,COVER_INNER_Z))
    near_x = x < CASE_W/2
    near_y = y < CASE_H/2
    gx0 = 5.0 if near_x else x-1.6
    gx1 = x+1.6 if near_x else CASE_W-5.0
    gy0 = 5.0 if near_y else y-1.6
    gy1 = y+1.6 if near_y else CASE_H-5.0
    gus_x=Part.makeBox(gx1-gx0,3.2,COVER_BOSS_ENGAGE,
                       App.Vector(gx0,y-1.6,COVER_INNER_Z))
    gus_y=Part.makeBox(3.2,gy1-gy0,COVER_BOSS_ENGAGE,
                       App.Vector(x-1.6,gy0,COVER_INNER_Z))
    print("cover boss %.1f,%.1f gusset overlap tub %.3f"%
          (x,y,chassis.common(gus_x.fuse(gus_y)).Volume))
    chassis=chassis.fuse(boss).fuse(gus_x).fuse(gus_y)
    pilot=Part.makeCylinder(COVER_PILOT_D/2,COVER_BOSS_ENGAGE+0.2,
                            App.Vector(x,y,COVER_INNER_Z-0.1))
    chassis=chassis.cut(pilot)

# Front-driven M2 screws, reachable through the front counterbore.
#
# The previous rear-driven ledge was unreachable: at every perimeter position an 7 mm driver
# path from the rear opening is blocked (measured 37-44 mm3) because the 8 mm rear taper's
# inner face crosses inward of the screw axis below z~7.5. The bezel band is only ~3.2 mm
# thick and the chassis wall ~1.8 mm, so the ONLY reachable scheme is a screw inserted from
# the front, through the bezel, into a chassis boss. Each boss needs an internal nut/thread
# feature, so it is built from the grid down into the wall with a wide base for print support.
SCREW_M2=2.2          # M2 through-hole clearance
NUT_AF=4.6            # M2 hex nut across-flats + 0.2 mm (captive pocket per published fits)
NUT_DEPTH=1.8
for x,y in FASTENERS:
    # Boss grows down from the grid underside into the tapered wall region. It is clipped to
    # the real shell so it cannot protrude outside the 134.4 mm outline.
    boss_raw=Part.makeCylinder(3.4,GRID_Z0-6.0,App.Vector(x,y,6.0))
    boss=boss_raw.common(outer)
    if boss.Volume<5.0:
        raise RuntimeError("fastener boss at %.1f,%.1f has no material"%(x,y))
    chassis=chassis.fuse(boss)
    # Through-hole must run the FULL length from the nut pocket to the split plane. Stopping
    # the cut at GRID_Z0-4.0 left the grid/bridge layer crossing the axis at z=14.2-14.5,
    # which the screw shank then hit (measured 0.63 mm3).
    chassis=chassis.cut(Part.makeCylinder(SCREW_M2/2,SPLIT_Z-4.0+0.4,App.Vector(x,y,4.0)))
    # Captive hex-nut pocket at the bottom, open downward so a nut drops in from the cavity.
    hexp=Part.makeCylinder(NUT_AF/2/math.cos(math.radians(30)),NUT_DEPTH,
                           App.Vector(x,y,6.0))
    chassis=chassis.cut(hexp)
    print("front fastener %.1f,%.1f boss %.1f mm3"%(x,y,boss.Volume))
# Registration comes from the bezel skirt fit plus these screws.

if len(chassis.Solids)!=1 or not chassis.isValid():
    raise RuntimeError("main chassis is not one valid printable solid")

doc=App.newDocument("MainChassisBase")
o=doc.addObject("Part::Feature","PRINT_MAIN_CHASSIS_BASE")
o.Label="PRINT: Rear electronics chassis + support grid (base before electronics mounts)"
o.Shape=chassis
o.addProperty("App::PropertyString","PrintOrientation").PrintOrientation="Front/grid face down; pillars and collar grow from grid; rear taper prints last"
o.addProperty("App::PropertyString","AssemblyRole").AssemblyRole="Panel is placed on foam/grid, then captured by separate front bezel"
o.addProperty("App::PropertyString","Material").Material="PETG"
o.addProperty("App::PropertyString","BuildStage").BuildStage="Board mounts and service bay added in final assembly stage"
doc.recompute(); doc.saveAs(os.path.join(HERE,"main_chassis_base.FCStd"))

b=chassis.optimalBoundingBox()
print("CHASSIS bbox %.2f x %.2f x %.2f z %.2f..%.2f"%(b.XLength,b.YLength,b.ZLength,b.ZMin,b.ZMax))
print("solid %d valid %s volume %.0f"%(len(chassis.Solids),chassis.isValid(),chassis.Volume))
print("cavity cross-section at z2.8: %.1f x %.1f; at z8+: %.1f x %.1f"%
      (CASE_W-2*(BACK_CHAMFER-COVER_INNER_Z+WALL),CASE_H-2*(BACK_CHAMFER-COVER_INNER_Z+WALL),
       CASE_W-2*WALL,CASE_H-2*WALL))
print("bezel fastener ledges",FASTENERS,"M2 clearance",SCREW_D,"blind pilot in bezel")
print("saved main_chassis_base.FCStd")
