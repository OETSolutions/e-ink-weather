"""Final printable main chassis from the closed panel -> FPC -> P5 -> PCB chain.

The ESP32-M1 is flipped component-side REARWARD, per the user's physical fit correction:
  local board -> Rx=180 deg -> Rz=-90 deg -> global translation

This keeps P5's opening pointed toward the panel tail (-Y), but turns the connector over so
the folded FPC enters from the correct face. The transform is solved from the P5 contact,
not positioned by eye. Mounts, battery collar and service openings all derive from it.
"""
import FreeCAD as App
import Part
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import (FPC_ROOT_X,FPC_ROOT_Y,FPC_ROOT_Z,FPC_LEN,FPC_BEND_R,
                                  PANEL_Z,GRID_Z0,PANEL_W,PANEL_H,MIN_PRINTED_WALL,
                                  MIN_LOAD_WALL,MIN_M2_THREAD)

HERE=os.path.dirname(os.path.abspath(__file__))

# ---- closed FPC / flipped-board constraint chain -------------------------------
ARC_LEN=math.pi*FPC_BEND_R
STRAIGHT_LEN=FPC_LEN-ARC_LEN
FPC_TIP_X=FPC_ROOT_X
FPC_TIP_Y=FPC_ROOT_Y+STRAIGHT_LEN
FPC_TIP_Z=FPC_ROOT_Z-2*FPC_BEND_R
# P5 reference point, read from the board model's own P5_ENTRY object rather than hardcoded.
# The tail tip must bottom out at the entry's INNER end (fully inserted). Using the connector
# BODY centre (45.55) instead placed the tip 1.75 mm THROUGH the connector and 3.45 mm past its
# mouth -- the "doesn't line up with the fpc" the user reported.
_board_src=App.openDocument(os.path.join(HERE,"esp32_m1.FCStd"))
_p5=_board_src.getObject("P5_ENTRY")
_p5bb=_p5.Shape.BoundBox
P5_CONTACT_X_LOCAL=_p5bb.XMin                        # inner end = full insertion depth
P5_CENTER_Y_LOCAL=(_p5bb.YMin+_p5bb.YMax)/2.0        # array centre
P5_CENTER_Z_LOCAL=(_p5bb.ZMin+_p5bb.ZMax)/2.0        # entry mid-height
# Rx180 sends (x,y,z)->(x,-y,-z); Rz-90 then sends xg=-y, yg=-x.
BOARD_TX=FPC_TIP_X+P5_CENTER_Y_LOCAL                  # xg=TX-local_y
BOARD_TY=FPC_TIP_Y+P5_CONTACT_X_LOCAL                 # yg=TY-local_x
BOARD_Z=FPC_TIP_Z+P5_CENTER_Z_LOCAL                   # zg=TZ-local_z


def board_transform(shape):
    s=shape.copy()
    s.rotate(App.Vector(0,0,0),App.Vector(1,0,0),180)
    s.rotate(App.Vector(0,0,0),App.Vector(0,0,1),-90)
    s.translate(App.Vector(BOARD_TX,BOARD_TY,BOARD_Z))
    return s


# ---- board / battery positions -------------------------------------------------
MOUNT_LOCAL=[(2.40,2.45),(45.60,2.45),(2.40,63.65),(45.60,63.65)]
MOUNTS=[(BOARD_TX-y,BOARD_TY-x) for x,y in MOUNT_LOCAL]
# After Rx180, PCB local z0 is the FRONT/non-component face at global BOARD_Z; local z1 is
# the rear/component-side face at BOARD_Z-1. Pillars connect the front face to the grid.
PCB_FRONT_Z=BOARD_Z
PILLAR_BOTTOM=PCB_FRONT_Z
# Pillars must run from the PCB front face up to the grid underside. With the R3.9 fold the
# board sits lower, so real engagement is available; the previous 1.0 mm pillars were not a
# credible standoff for an M2 thread.
PILLAR_TOP=GRID_Z0                                     # reach the actual grid underside
PILLAR_R=3.2
# The standoff must contain an M2 self-tapping pilot, so its controlling minimum is the
# thread engagement (2.0 mm), not an arbitrary length. The 24 mm FPC tail bounds this from
# above: a deeper fold (R3.9) would drive the tail into the side wall, so R2.9 is the deepest
# collision-free fold and yields 2.22 mm of standoff -- above the 2.0 mm thread minimum and a
# continuous solid column (verified ~70 mm2 cross-section). The board is additionally
# located by the battery collar and the service face.
PILLAR_MIN_ENGAGE=MIN_M2_THREAD
PILOT_D=1.6                                            # M2 self-tapping pilot
# Battery moves to the right side of the flipped board, beside the PCB, user's 70x39x11.
BAT_X0,BAT_X1=86.0,125.0                               # DERIVED from flipped board x<=83.3
BAT_Y0,BAT_Y1=18.0,88.0
BAT_Z0,BAT_Z1=3.5,14.5                                 # shared stack planes
BAT_CLEAR=0.5
COLLAR_W=MIN_PRINTED_WALL                              # PETG locating collar
COLLAR_Z0,COLLAR_Z1=GRID_Z0-1.2,GRID_Z0+0.2            # hangs from grid, overlaps it

# ---- real transformed port keep-outs -------------------------------------------
board_doc=App.openDocument(os.path.join(HERE,"esp32_m1.FCStd"))
PORT_KEEP_OUTS=[("KEY","CLR_KEY"),("MICRO_SD","CLR_MICROSD"),
                ("POWER","CLR_POWER"),("USB_C","CLR_USB_C")]
transformed_ports=[]
for name,objname in PORT_KEEP_OUTS:
    src=board_doc.getObject(objname)
    if not src: raise RuntimeError("missing named board keep-out "+objname)
    transformed_ports.append((name,board_transform(src.Shape)))

# All user-service interfaces still face +Y. The plate lies through the transformed keep-outs;
# X extents and vertical range are derived from those keep-outs after the final transform.
SERVICE_Y0,SERVICE_Y1=69.4,70.6
SERVICE_WALL=SERVICE_Y1-SERVICE_Y0
SERVICE_X0=min(s.BoundBox.XMin for _,s in transformed_ports)-2.5
SERVICE_X1=max(s.BoundBox.XMax for _,s in transformed_ports)+2.5
SERVICE_Z0=min(s.BoundBox.ZMin for _,s in transformed_ports)-0.2
SERVICE_Z1=GRID_Z0

base_doc=App.openDocument(os.path.join(HERE,"main_chassis_base.FCStd"))
chassis=base_doc.getObject("PRINT_MAIN_CHASSIS_BASE").Shape
if PILLAR_TOP-PILLAR_BOTTOM < PILLAR_MIN_ENGAGE-1e-6:
    raise RuntimeError("board pillar engagement %.2f mm below %.2f mm minimum"%
                       (PILLAR_TOP-PILLAR_BOTTOM,PILLAR_MIN_ENGAGE))

# Board pillars descend from the front carrier. Screws enter through the component-side/rear
# PCB holes and thread into these blind pillars; no fastener passes behind the display.
for x,y in MOUNTS:
    pillar=Part.makeCylinder(PILLAR_R,PILLAR_TOP-PILLAR_BOTTOM,App.Vector(x,y,PILLAR_BOTTOM))
    candidate=chassis.fuse(pillar)
    print("pillar at %.2f,%.2f: solids %d -> %d intersection %.3f"%
          (x,y,len(chassis.Solids),len(candidate.Solids),chassis.common(pillar).Volume))
    chassis=candidate
    pilot=Part.makeCylinder(PILOT_D/2,PILLAR_TOP-PILLAR_BOTTOM+0.2,
                            App.Vector(x,y,PILLAR_BOTTOM-0.1))
    chassis=chassis.cut(pilot)

# Battery top-locating collar; rear cover clamps the cell axially.
outer=Part.makeBox((BAT_X1-BAT_X0)+2*(BAT_CLEAR+COLLAR_W),
                   (BAT_Y1-BAT_Y0)+2*(BAT_CLEAR+COLLAR_W),COLLAR_Z1-COLLAR_Z0,
                   App.Vector(BAT_X0-BAT_CLEAR-COLLAR_W,BAT_Y0-BAT_CLEAR-COLLAR_W,COLLAR_Z0))
inner=Part.makeBox((BAT_X1-BAT_X0)+2*BAT_CLEAR,
                   (BAT_Y1-BAT_Y0)+2*BAT_CLEAR,COLLAR_Z1-COLLAR_Z0+0.2,
                   App.Vector(BAT_X0-BAT_CLEAR,BAT_Y0-BAT_CLEAR,COLLAR_Z0-0.1))
collar=outer.cut(inner)
print("collar intersection %.3f solids before %d"%(chassis.common(collar).Volume,len(chassis.Solids)))
chassis=chassis.fuse(collar)
print("after collar solids",len(chassis.Solids))

# --- NO service plate ---------------------------------------------------------------
# The former plate sat at y=69.4..70.6, but the transformed connectors' +Y edges are at
# y=64.99 (microSD), 65.14 (power), 65.94 (KEY) and 67.34 (USB-C). It was 2-5 mm BEYOND every
# connector, so it never constrained anything, and its "ports" lined up with nothing. Removed
# entirely; the connectors now open directly into the rear service bay and hatch.
port_boxes=[]
for name,keep in transformed_ports:
    kb=keep.BoundBox
    port_boxes.append((name,kb.XMin,kb.XMax,kb.ZMin,kb.ZMax))
print("service plate REMOVED (was %.1f mm beyond the nearest connector)"%(
    SERVICE_Y0-max(kb.YMax for _,kb in ((n,k.BoundBox) for n,k in transformed_ports))))

if len(chassis.Solids)!=1 or not chassis.isValid():
    raise RuntimeError("final chassis is not one valid printable solid")

doc=App.newDocument("MainChassisFinal")
o=doc.addObject("Part::Feature","PRINT_MAIN_CHASSIS")
o.Label="PRINT: Main chassis, flipped-board mounts + battery collar + service face"
o.Shape=chassis
o.addProperty("App::PropertyString","PrintOrientation").PrintOrientation="Front/grid face down; pillars, collar and service face grow from grid; rear taper prints last"
o.addProperty("App::PropertyString","ConstraintChain").ConstraintChain=(
    "Panel root -> 24 mm FPC R2.4 180deg -> P5 -> PCB Rx180 then Rz-90; component side rearward")
o.addProperty("App::PropertyString","BoardTransform").BoardTransform=(
    "Rx=180; Rz=-90; T=(%.3f, %.3f, %.3f)"%(BOARD_TX,BOARD_TY,BOARD_Z))

doc.recompute(); doc.saveAs(os.path.join(HERE,"main_chassis.FCStd"))

print("FPC arc %.3f + straight %.3f = %.3f"%(ARC_LEN,STRAIGHT_LEN,ARC_LEN+STRAIGHT_LEN))
print("FPC tip/contact (%.3f, %.3f, %.3f)"%(FPC_TIP_X,FPC_TIP_Y,FPC_TIP_Z))
print("BOARD Rx=180 Rz=-90 T=(%.3f, %.3f, %.3f)"%(BOARD_TX,BOARD_TY,BOARD_Z))
print("PCB envelope x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f"%
      (BOARD_TX-66,BOARD_TX,BOARD_TY-48,BOARD_TY,BOARD_Z-1,BOARD_Z))
print("MOUNTS",[(round(x,3),round(y,3)) for x,y in MOUNTS])
print("BATTERY x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f"%(BAT_X0,BAT_X1,BAT_Y0,BAT_Y1,BAT_Z0,BAT_Z1))
print("board/battery physical gap %.3f"%(BAT_X0-83.3))
print("SERVICE BAY TARGET X %.2f..%.2f Z %.2f..%.2f"%(SERVICE_X0,SERVICE_X1,SERVICE_Z0,SERVICE_Z1))
for p in port_boxes: print("PORT_KEEP_OUT",p)
b=chassis.optimalBoundingBox()
print("CHASSIS bbox %.2f x %.2f x %.2f solid %d valid %s vol %.0f"%
      (b.XLength,b.YLength,b.ZLength,len(chassis.Solids),chassis.isValid(),chassis.Volume))
print("saved main_chassis.FCStd")
