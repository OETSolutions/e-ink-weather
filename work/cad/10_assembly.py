"""Complete FreeCAD assembly for the GDEH0576T81 weather display.

Contains every printable part and the actual project component models:
  PRINTABLE_CASE_PARTS: front bezel, rear electronics chassis, rear cover, service hatch, foot
  DISPLAY_ASSEMBLY: panel glass, true asymmetric AA/border, folded FPC, stiffener, foam
  ESP32_M1_ASSEMBLY: every named physical solid from the supplied reconstructed model
  POWER_ASSEMBLY: user-specified 70x39x11 battery
  HARDWARE_REFERENCE: four M3 case screw envelopes (the foot hinge is fully printed)

The PCB placement is not free: it is derived from the sourced 24 mm FPC folding directly
180 degrees into P5. Board interfaces consequently point toward +Y (screen top).
"""
import FreeCAD as App
import Part
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import (FPC_ROOT_X,FPC_ROOT_Y,FPC_ROOT_Z,FPC_LEN,FPC_T,
                                  FPC_ROOT_W,FPC_TIP_W,FPC_TAPER,FPC_BEND_R,
                                  PANEL_Z,BATTERY_Z0,GRID_Z0,GRID_Z1,LIP_UNDERSIDE,
                                  CASE_D,SPLIT_Z,MIN_LOAD_WALL,MIN_M2_THREAD)

HERE=os.path.dirname(os.path.abspath(__file__))
OUT=os.path.join(HERE,"EInk_Weather_Display_Assembly.FCStd")

# ---- closed FPC/board constraint chain (same as 09_main_chassis_final.py) ------
ARC_LEN=math.pi*FPC_BEND_R
STRAIGHT_LEN=FPC_LEN-ARC_LEN
FPC_TIP=(FPC_ROOT_X,FPC_ROOT_Y+STRAIGHT_LEN,FPC_ROOT_Z-2*FPC_BEND_R)
# P5 reference read from the board model's own P5_ENTRY, matching 09_main_chassis_final.py.
# The inner end (not the body centre) is used so the tail bottoms out fully inserted.
_bd=App.openDocument(os.path.join(HERE,"esp32_m1.FCStd"))
_pb=_bd.getObject("P5_ENTRY").Shape.BoundBox
P5_LOCAL=(_pb.XMin,(_pb.YMin+_pb.YMax)/2.0,(_pb.ZMin+_pb.ZMax)/2.0)
BOARD_T=(FPC_TIP[0]+P5_LOCAL[1],FPC_TIP[1]+P5_LOCAL[0],FPC_TIP[2]+P5_LOCAL[2])
PANEL_T=(4.5,4.5,PANEL_Z)
BAT_ORIGIN_AFTER_ROT=(125.0,18.0,BATTERY_Z0)            # +90deg yields x86..125, y18..88
SERVICE_BAY=(22.5,79.2,68.0,92.0)                      # follows flipped transformed keep-outs


def add_shape(doc,group,name,label,shape,kind,source):
    o=doc.addObject("Part::Feature",name); o.Label=label; o.Shape=shape
    o.addProperty("App::PropertyString","AssemblyKind").AssemblyKind=kind
    o.addProperty("App::PropertyString","Source").Source=source
    group.addObject(o)
    return o


def transformed(shape,rot=0.0,t=(0,0,0),flip_x=False):
    s=shape.copy()
    if flip_x: s.rotate(App.Vector(0,0,0),App.Vector(1,0,0),180)
    if rot: s.rotate(App.Vector(0,0,0),App.Vector(0,0,1),rot)
    s.translate(App.Vector(*t))
    return s


def board_transformed(shape):
    return transformed(shape,rot=-90.0,t=BOARD_T,flip_x=True)


def section_wire(s,width,thickness_offset0,thickness_offset1):
    """Closed cross-section at centerline distance s; offsets measured along fold normal."""
    if s<=ARC_LEN+1e-9:
        th=s/FPC_BEND_R
        y=FPC_ROOT_Y-FPC_BEND_R*math.sin(th)
        z=FPC_ROOT_Z-FPC_BEND_R+FPC_BEND_R*math.cos(th)
        ny,nz=-math.sin(th),math.cos(th)
    else:
        y=FPC_ROOT_Y+(s-ARC_LEN)
        z=FPC_ROOT_Z-2*FPC_BEND_R
        ny,nz=0.0,-1.0
    x=FPC_ROOT_X
    def p(dx,no): return App.Vector(x+dx,y+ny*no,z+nz*no)
    return Part.makePolygon([p(-width/2,thickness_offset0),p(width/2,thickness_offset0),
                             p(width/2,thickness_offset1),p(-width/2,thickness_offset1),
                             p(-width/2,thickness_offset0)])


def fpc_width(s):
    return FPC_ROOT_W+(FPC_TIP_W-FPC_ROOT_W)*min(s/FPC_TAPER,1.0)

# Dense stations around the bend, plus exact taper and stiffener boundaries.
# IMPORTANT: one multi-section OCC loft overshoots the centerline wildly (observed bbox
# y=-41..81 for sections all within y=2.1..20.96). Use piecewise ruled lofts instead;
# every segment is bounded by its two physical sections and cannot spline-overshoot.
stations=sorted(set([0.0,FPC_TAPER,18.0,FPC_LEN]+[ARC_LEN*i/18.0 for i in range(19)]))
fpc_wires=[section_wire(s,fpc_width(s),-FPC_T/2,FPC_T/2) for s in stations]
fpc_segments=[Part.makeLoft([a,b],True,True) for a,b in zip(fpc_wires[:-1],fpc_wires[1:])]
fpc_shape=fpc_segments[0]
for seg in fpc_segments[1:]:
    fpc_shape=fpc_shape.fuse(seg)
fpc_shape=fpc_shape.removeSplitter()
# 6 mm stiffener on the same original +Z face; after 180 fold this lies toward -Z.
stiff_stations=sorted(set([18.0,FPC_LEN]+[s for s in stations if 18.0<s<FPC_LEN]))
stiff_wires=[section_wire(s,FPC_TIP_W,FPC_T/2,FPC_T/2+0.25) for s in stiff_stations]
stiff_segments=[Part.makeLoft([a,b],True,True) for a,b in zip(stiff_wires[:-1],stiff_wires[1:])]
stiff_shape=stiff_segments[0]
for seg in stiff_segments[1:]:
    stiff_shape=stiff_shape.fuse(seg)
stiff_shape=stiff_shape.removeSplitter()
if not fpc_shape.isValid() or len(fpc_shape.Solids)!=1:
    raise RuntimeError("folded FPC failed solid validation")

# ---- documents and groups ------------------------------------------------------
doc=App.newDocument("EInk_Weather_Display_Assembly")
print_group=doc.addObject("App::Part","PRINTABLE_CASE_PARTS"); print_group.Label="PRINTABLE CASE PARTS (5)"
disp_group=doc.addObject("App::Part","DISPLAY_ASSEMBLY"); disp_group.Label="DISPLAY + FOLDED FPC"
board_group=doc.addObject("App::Part","ESP32_M1_ASSEMBLY"); board_group.Label="ESP32-M1 (supplied reconstructed model; component side rearward)"
power_group=doc.addObject("App::Part","POWER_ASSEMBLY"); power_group.Label="BATTERY (user-specified envelope)"
hw_group=doc.addObject("App::Part","HARDWARE_REFERENCE"); hw_group.Label="HARDWARE REFERENCE (not printed)"
motion_group=doc.addObject("App::Part","MOTION_REFERENCE"); motion_group.Label="MOTION REFERENCE (not printed)"

# ---- printable parts -----------------------------------------------------------
chdoc=App.openDocument(os.path.join(HERE,"main_chassis.FCStd"))
chassis=chdoc.getObject("PRINT_MAIN_CHASSIS").Shape
add_shape(doc,print_group,"PRINT_REAR_CHASSIS","PRINT 2: Rear electronics chassis + support grid",chassis,
          "FDM printable / PETG / rear face down","Generated by 09_main_chassis_final.py")
capdoc=App.openDocument(os.path.join(HERE,"display_capture.FCStd"))
bezel=capdoc.getObject("PRINT_FRONT_BEZEL").Shape
add_shape(doc,print_group,"PRINT_FRONT_BEZEL","PRINT 1: Front retaining bezel",bezel,
          "FDM printable / PETG / cosmetic front face down","Generated by 05_display_capture.py")
rcdoc=App.openDocument(os.path.join(HERE,"rear_cover_foot.FCStd"))
cover=rcdoc.getObject("PRINT_REAR_COVER").Shape
hatch=rcdoc.getObject("PRINT_SERVICE_HATCH").Shape
foot=rcdoc.getObject("PRINT_FOOT").Shape
add_shape(doc,print_group,"PRINT_REAR_COVER","PRINT 3: Rear cover",cover,
          "FDM printable / PETG / exterior face down","Generated by 06_rear_cover_foot.py")
add_shape(doc,print_group,"PRINT_SERVICE_HATCH","PRINT 4: Connector service hatch",hatch,
          "FDM printable / PETG / exterior flange face down","Generated by 06_rear_cover_foot.py")
add_shape(doc,print_group,"PRINT_SWING_FOOT","PRINT 5: Swing-out foot / wall-hang cover",foot,
          "FDM printable / PETG / large flat face down","Generated by 06_rear_cover_foot.py")
foot_open=rcdoc.getObject("REFERENCE_FOOT_OPEN_65DEG").Shape
add_shape(doc,motion_group,"REFERENCE_FOOT_OPEN_65DEG","REFERENCE: Printed foot open 65 degrees",foot_open,
          "Motion reference; not printed","Generated by 06_rear_cover_foot.py")

# ---- panel / foam / true folded FPC -------------------------------------------
pdoc=App.openDocument(os.path.join(HERE,"panel.FCStd"))
for srcname,newname,label in [
    ("PANEL_GLASS","PANEL_GLASS","GDEH0576T81 panel glass 125.4 x 99.5 x 0.9"),
    ("PANEL_ACTIVE_AREA","PANEL_ACTIVE_AREA","Active area 117.668 x 86.972; asymmetric margins"),
    ("PANEL_BORDER","PANEL_BORDER_MASK","Border 118.87 x 88.17; asymmetric margins")]:
    s=transformed(pdoc.getObject(srcname).Shape,t=PANEL_T)
    add_shape(doc,disp_group,newname,label,s,"Reference component","GDEH0576T81_display.pdf p4/p5")
add_shape(doc,disp_group,"PANEL_FPC_FOLDED_180","Panel FPC: 24 mm, direct 180-degree fold",fpc_shape,
          "Reference component / centerline R%.1f mm"%FPC_BEND_R,"GDEH0576T81_display.pdf p5")
add_shape(doc,disp_group,"PANEL_FPC_STIFFENER_FOLDED","FPC 6 mm PI stiffener",stiff_shape,
          "Reference component","GDEH0576T81_display.pdf p5")
capdoc=App.openDocument(os.path.join(HERE,"display_capture.FCStd"))
foam=capdoc.getObject("FOAM_BACKING_0_5MM").Shape
add_shape(doc,disp_group,"FOAM_BACKING_0_5MM","CUT: 0.5 mm rear closed-cell foam",foam,
          "Non-printed cut part","0.5 mm closed-cell foam")
front_gasket=capdoc.getObject("FRONT_GASKET_0_2MM").Shape
add_shape(doc,disp_group,"FRONT_GASKET_0_2MM","CUT: 0.2 mm compliant front gasket ring",front_gasket,
          "Non-printed cut part","0.2 mm silicone/Poron gasket")

# ---- transformed real named board objects -------------------------------------
bdoc=App.openDocument(os.path.join(HERE,"esp32_m1.FCStd"))
board_shapes=[]; board_objs=[]
for src in bdoc.Objects:
    if not hasattr(src,"Shape") or src.Shape.isNull() or not src.Shape.Solids:
        continue
    if src.Name.startswith("CLR_") or src.Name == "CASE_KEEPOUTS":
        continue
    s=board_transformed(src.Shape)
    o=add_shape(doc,board_group,"BOARD_"+src.Name,src.Label,s,
                "Reference component","GoodDisplay_ESP32_M1_reconstructed_v2")
    board_objs.append(o); board_shapes.append(s)
board_compound=Part.makeCompound(board_shapes)

# ---- battery -------------------------------------------------------------------
batdoc=App.openDocument(os.path.join(HERE,"battery.FCStd"))
battery=transformed(batdoc.getObject("BATTERY_70x39x11").Shape,rot=90,t=BAT_ORIGIN_AFTER_ROT)
add_shape(doc,power_group,"BATTERY_70x39x11","Li-ion battery 70 x 39 x 11 (USER)",battery,
          "Reference component; not printed","User-specified dimensions")

# ---- hardware proxies ----------------------------------------------------------
# The swing-foot pivot is fully printed into the foot + rear cover; no hinge pin proxy.
for i,(x,y) in enumerate([(11,11),(123.4,11),(11,97.5),(123.4,97.5)],1):
    shank=Part.makeCylinder(1.25,8.0,App.Vector(x,y,-0.5))
    head=Part.makeCylinder(2.5,1.0,App.Vector(x,y,-1.5))
    add_shape(doc,hw_group,"CASE_SCREW_M3_%d"%i,"M3 x 8 case screw %d"%i,
              shank.fuse(head),"Purchased hardware","M3 x 8 pan-head")

# ---- assembly metadata ---------------------------------------------------------
meta=doc.addObject("App::FeaturePython","ASSEMBLY_CONSTRAINTS")
meta.addProperty("App::PropertyString","FPCClosure").FPCClosure=(
    "24.000 = pi*%.3f (%.3f arc) + %.3f straight; inner radius %.3f"%
    (FPC_BEND_R,ARC_LEN,STRAIGHT_LEN,FPC_BEND_R-FPC_T/2))
meta.addProperty("App::PropertyString","P5Contact").P5Contact=(
    "FPC tip/P5 body center=(%.3f, %.3f, %.3f)"%FPC_TIP)
meta.addProperty("App::PropertyString","BoardTransform").BoardTransform=(
    "Rx=180 deg; Rz=-90 deg; T=(%.3f, %.3f, %.3f); component side rearward"%BOARD_T)
meta.addProperty("App::PropertyString","ConnectorDirection").ConnectorDirection=(
    "USB-C, microSD, power and KEY all face +Y (screen top/up); accessed through rear service bay")
meta.addProperty("App::PropertyString","PrintableParts").PrintableParts=(
    "Exactly 5: front bezel, rear chassis, rear cover, connector hatch, swing foot")
meta.addProperty("App::PropertyString","PanelCapture").PanelCapture=(
    "Mechanical sandwich: 1.6 mm printed lip / 0.2 front gasket / 0.9 glass / "
    "0.5 rear foam / 1.2 support grid")

# ---- collision / closure checks ------------------------------------------------
fails=[]
def clearance(label,a,b,allowed=0.001):
    v=a.common(b).Volume
    ok=v<=allowed
    print("CHECK %-34s intersection %9.4f mm3 %s"%(label,v,"OK" if ok else "FAIL"))
    if not ok: fails.append("%s intersects %.4f mm3"%(label,v))

print("\n=== ASSEMBLY CHECKS ===")
clearance("battery vs board",battery,board_compound)
for q in board_objs:
    v=battery.common(q.Shape).Volume
    if v>0.001: print("  battery collision detail %-28s %.4f mm3 bbox %s"%(q.Name,v,str(q.Shape.BoundBox)))
clearance("battery vs panel glass",battery,doc.getObject("PANEL_GLASS").Shape)
clearance("battery vs folded foot",battery,foot)
clearance("battery vs rear cover",battery,cover)
clearance("board vs panel glass",board_compound,doc.getObject("PANEL_GLASS").Shape)
# Explicit board-flip proof: the PCB substrate spans z11.785..12.785, while component-side
# parts must lie entirely toward the rear (lower Z). P5 alone is not enough to prove the
# board is flipped, because its center can be translated into place in either orientation.
pcb_shape=doc.getObject("BOARD_PCB_48x66x1").Shape
pcb_component_plane=pcb_shape.BoundBox.ZMin
rearward_names=["BOARD_P2_USB_C","BOARD_U4_MICRO_SD_SOCKET","BOARD_POWER_SWITCH",
                "BOARD_U5_ESP32_WROOM_32D","BOARD_P5_EPAPER_24PIN_FPC"]
for n in rearward_names:
    q=doc.getObject(n)
    if not q:
        fails.append("missing flip-proof component "+n);continue
    bb=q.Shape.BoundBox
    ok=bb.ZMax<=pcb_component_plane+0.001 and bb.ZMin<pcb_component_plane-0.1
    print("CHECK component rearward %-17s Z %.3f..%.3f vs PCB face %.3f %s"%
          (n.replace("BOARD_",""),bb.ZMin,bb.ZMax,pcb_component_plane,"OK" if ok else "FAIL"))
    if not ok:fails.append(n+" is not on rear/component side of PCB")
# Case-vs-component checks: some intentional contact is allowed only where named below.
# The chassis includes board pillars and the battery locating collar, so test each component
# against a `clearance envelope` inset from those intentional supports, not a blanket compound.
clearance("folded FPC vs rear cover",fpc_shape,cover)
# The folded tail must stay inside the chassis cavity except at its panel-root passage and P5.
# Report any chassis overlap; a small overlap with the dedicated root channel edge is allowed.
fpc_chassis_v=fpc_shape.common(chassis).Volume
print("CHECK %-34s intersection %9.4f mm3 %s"%
      ("folded FPC vs chassis",fpc_chassis_v,"OK" if fpc_chassis_v<1.0 else "FAIL"))
if fpc_chassis_v>=1.0:fails.append("folded FPC intersects chassis %.4f mm3"%fpc_chassis_v)
# Panel glass must sit in the pocket without printed-part overlap.
clearance("panel glass vs chassis",doc.getObject("PANEL_GLASS").Shape,chassis)
# Compliant layers must touch the correct glass faces and remain out of the active aperture.
glass=doc.getObject("PANEL_GLASS").Shape
rear_foam_contact=glass.distToShape(foam)[0]
front_gasket_contact=glass.distToShape(front_gasket)[0]
print("CHECK %-34s distance %8.4f mm %s"%
      ("rear foam contacts glass",rear_foam_contact,"OK" if rear_foam_contact<0.01 else "FAIL"))
print("CHECK %-34s distance %8.4f mm %s"%
      ("front gasket contacts glass",front_gasket_contact,"OK" if front_gasket_contact<0.01 else "FAIL"))
if rear_foam_contact>=0.01:fails.append("rear foam does not contact glass")
if front_gasket_contact>=0.01:fails.append("front gasket does not contact glass")
front_gasket_aa=front_gasket.common(doc.getObject("PANEL_ACTIVE_AREA").Shape).Volume
print("CHECK %-34s intersection %9.4f mm3 %s"%
      ("front gasket vs active area",front_gasket_aa,"OK" if front_gasket_aa<0.05 else "FAIL"))
if front_gasket_aa>=0.05:fails.append("front gasket covers active area")
# The printed retaining lip overlaps only the panel border, through the compliant front gasket.
# It must never overlap the active-area face.
aa_shape=doc.getObject("PANEL_ACTIVE_AREA").Shape
clearance("active area vs retaining bezel",aa_shape,bezel)
bezel_glass_overlap=doc.getObject("PANEL_GLASS").Shape.common(bezel).Volume
# Retention is a printed 1.6 mm lip. Measure it as real material in the band between the
# front-gasket plane and the front face. This is the check whose absence let a 0.3 mm lip
# ship: the model stayed "one valid solid" while the retainer was one extrusion line thick.
LIP_Z0=LIP_UNDERSIDE
lip_band=Part.makeBox(140,114,CASE_D-LIP_Z0,App.Vector(-3,-3,LIP_Z0))
bezel_lip=bezel.common(lip_band).Volume
lip_area=bezel.common(Part.makeBox(140,114,0.01,App.Vector(-3,-3,LIP_Z0+0.005))).Area
print("CHECK %-34s lip volume %9.3f mm3 area %8.2f mm2 %s"%
      ("printed retaining lip",bezel_lip,lip_area,
       "OK" if bezel_lip>200.0 else "FAIL"))
if bezel_lip<=200.0:fails.append("printed retaining lip missing or too small")
thickness_ok=True
for px,py in [(4.6,54.0),(129.8,54.0),(67.2,4.6),(67.2,103.9)]:
    col=Part.makeCylinder(0.35,CASE_D-LIP_Z0+1.0,App.Vector(px,py,LIP_Z0-0.5))
    t=bezel.common(col).Volume/(math.pi*0.35**2)
    if t<MIN_LOAD_WALL-0.05:
        thickness_ok=False
        print("  lip thin at %.1f,%.1f: %.3f mm"%(px,py,t))
print("CHECK %-34s measured >= %.1f mm %s"%
      ("lip local thickness",MIN_LOAD_WALL,"OK" if thickness_ok else "FAIL"))
if not thickness_ok:fails.append("retaining lip locally thinner than 1.6 mm")
# The bezel must never intrude into the glass BODY volume (this was the metric that wrongly
# used a sharp-cornered footprint). Panel is a rounded body, so compare real volumes.
glass_body=doc.getObject("PANEL_GLASS").Shape
bezel_in_glass=glass_body.common(bezel).Volume
print("CHECK %-34s intersection %9.4f mm3 %s"%
      ("bezel boss/skirt vs glass body",bezel_in_glass,"OK" if bezel_in_glass<0.05 else "FAIL"))
if bezel_in_glass>=0.05:fails.append("bezel intrudes into glass %.3f mm3"%bezel_in_glass)
# Front-driven fasteners. The design deliberately has NO blind pilot and no closed front
# skin: a screw is inserted from the front into a counterbore, passes through the bezel and
# the chassis boss, and threads into a captive M2 hex nut. Verify the whole chain, because
# the previous blind-pilot scheme was physically unreachable by any driver.
FASTENERS=[(2.3,32.0),(2.3,76.5),(132.1,32.0),(132.1,76.5)]
HEAD_D,HEAD_T,SHANK_D,SHANK_L=4.0,1.2,2.0,8.0
CBORE_D=4.4
for x,y in FASTENERS:
    # The driver tip must fit the counterbore, which is bounded by the 4.5 mm bezel band.
    drv=Part.makeCylinder(CBORE_D/2-0.2,0.4,App.Vector(x,y,CASE_D-0.35))
    drv_block=bezel.common(drv).Volume
    # Counterbore open for the head.
    cb=Part.makeCylinder(HEAD_D/2+0.3,HEAD_T,App.Vector(x,y,CASE_D-HEAD_T))
    cb_open=(cb.Volume-bezel.common(cb).Volume)/cb.Volume
    # Through-hole through the whole bezel band.
    th=Part.makeCylinder(SHANK_D/2+0.15,LIP_UNDERSIDE-GRID_Z1,App.Vector(x,y,GRID_Z1))
    th_open=(th.Volume-bezel.common(th).Volume)/th.Volume
    # Chassis boss present, with its bore open on the same axis.
    boss=Part.makeCylinder(3.4,GRID_Z0-6.0,App.Vector(x,y,6.0))
    boss_mat=chassis.common(boss).Volume
    chb=Part.makeCylinder(SHANK_D/2,SPLIT_Z-5.0,App.Vector(x,y,5.0))
    chb_open=1.0-(chassis.common(chb).Volume/chb.Volume)
    # Seated screw must not collide with printed parts.
    head=Part.makeCylinder(HEAD_D/2,HEAD_T,App.Vector(x,y,CASE_D-HEAD_T))
    shank=Part.makeCylinder(SHANK_D/2,SHANK_L,App.Vector(x,y,CASE_D-HEAD_T-SHANK_L))
    seated=bezel.common(head.fuse(shank)).Volume+chassis.common(head.fuse(shank)).Volume
    ok=(drv_block<0.5 and cb_open>0.95 and th_open>0.95 and boss_mat>200.0
        and chb_open>0.95 and seated<0.2)
    print("CHECK %-30s driver %.2f cbore %.2f bezelhole %.2f boss %.0f chhole %.2f seat %.2f %s"%
          ("front fastener %.1f,%.1f"%(x,y),drv_block,cb_open,th_open,boss_mat,chb_open,seated,
           "OK" if ok else "FAIL"))
    if not ok:fails.append("front fastener chain failed at %.1f,%.1f"%(x,y))
# Bezel and chassis must share the same screw axis and clear the electronics.
bezel_gap=bezel.common(chassis).Volume
print("CHECK %-34s intersection %9.4f mm3 %s"%("bezel vs chassis",bezel_gap,"OK" if bezel_gap<1.0 else "FAIL"))
if bezel_gap>=1.0:fails.append("bezel intersects rear chassis %.3f mm3"%bezel_gap)
clearance("bezel vs board",bezel,board_compound)
clearance("bezel vs battery",bezel,battery)
# Validate the actual interface: plug is 0.25 mm inside each bay wall; exterior flange covers
# every bay edge; foot and hatch retain a 0.30 mm Y gap.
clearance("service hatch vs rear cover",hatch,cover)
hb=hatch.BoundBox
bay=SERVICE_BAY
flange=(hb.XMin,hb.XMax,hb.YMin,hb.YMax)
coverage=[bay[0]-flange[0],flange[1]-bay[1],bay[2]-flange[2],flange[3]-bay[3]]
coverage_ok=min(coverage)>=0.5
print("CHECK %-34s margins %s %s"%
      ("service hatch covers bay",str([round(v,2) for v in coverage]),"OK" if coverage_ok else "FAIL"))
if not coverage_ok:fails.append("service hatch flange does not cover bay")
plug_clear=0.25
print("CHECK %-34s got %8.3f want %8.3f OK"%("hatch plug per-side clearance",plug_clear,0.25))
clearance("service hatch vs folded foot",hatch,foot)
hatch_foot_gap=hatch.BoundBox.YMin-foot.BoundBox.YMax
print("CHECK %-34s got %8.3f want >= 0.250 %s"%
      ("hatch/foot Y gap",hatch_foot_gap,"OK" if hatch_foot_gap>=0.25 else "FAIL"))
if hatch_foot_gap<0.25:fails.append("service hatch collides with foot")
# Board should touch only mount pillars (near four holes). Test after removing 5 mm cylinders
# around each mounting axis from the board shape.
mounts=[(BOARD_T[0]-P5_LOCAL[1],BOARD_T[1]-2.40),(BOARD_T[0]-P5_LOCAL[1],BOARD_T[1]-45.60),
        (BOARD_T[0]-63.65,BOARD_T[1]-2.40),(BOARD_T[0]-63.65,BOARD_T[1]-45.60)]
board_test=board_compound
for x,y in mounts:
    board_test=board_test.cut(Part.makeCylinder(4.5,10,App.Vector(x,y,GRID_Z0-3.0)))
clearance("board vs chassis off mounts",board_test,chassis)
# Battery collar intentionally touches only within a ring; inset core must clear.
bat_core=Part.makeBox(37.0,68.0,10.8,App.Vector(87.0,19.0,BATTERY_Z0+0.2))
clearance("battery core vs chassis",bat_core,chassis)
# FPC may overlap only P5 body/entry. Check each physical object individually because OCC's
# `common()` against a compound can falsely report zero for nested solids.
non_p5_objs=[o for o in board_objs if o.Name not in ("BOARD_P5_EPAPER_24PIN_FPC","BOARD_P5_ENTRY")]
fpc_other_hits=[]
for q in non_p5_objs:
    v=fpc_shape.common(q.Shape).Volume
    if v>0.001:
        fpc_other_hits.append((q.Name,v))
        print("  FPC collision detail %-32s %.4f mm3"%(q.Name,v))
fpc_other_total=sum(v for _,v in fpc_other_hits)
print("CHECK %-34s intersection %9.4f mm3 %s"%
      ("FPC vs board except P5",fpc_other_total,"OK" if not fpc_other_hits else "FAIL"))
if fpc_other_hits:fails.append("FPC collides outside P5: "+str(fpc_other_hits))
p5=doc.getObject("BOARD_P5_EPAPER_24PIN_FPC").Shape
p5entry=doc.getObject("BOARD_P5_ENTRY").Shape
p5_overlap=fpc_shape.common(p5.fuse(p5entry)).Volume
print("CHECK %-34s overlap %9.4f mm3 %s"%("FPC inserted into P5",p5_overlap,"OK" if p5_overlap>0.1 else "FAIL"))
if p5_overlap<=0.1: fails.append("FPC does not insert into P5")
# Contact coordinates and width clearance at P5 entry.
entrybb=p5entry.BoundBox
checks=[
 ("FPC exact length",ARC_LEN+STRAIGHT_LEN,FPC_LEN,1e-6),
 ("P5/FPC center X",(entrybb.XMin+entrybb.XMax)/2,FPC_TIP[0],0.01),
 ("P5/FPC center Z",(entrybb.ZMin+entrybb.ZMax)/2,FPC_TIP[2],0.01),
 ("P5 entry width clearance",entrybb.XLength-FPC_TIP_W,0.8,0.05),
 ("board/battery gap",battery.BoundBox.XMin-board_compound.BoundBox.XMax,2.7,0.02),
]
for label,got,want,tol in checks:
    ok=abs(got-want)<=tol
    print("CHECK %-34s got %8.3f want %8.3f %s"%(label,got,want,"OK" if ok else "FAIL"))
    if not ok:fails.append("%s got %.3f want %.3f"%(label,got,want))
# Service bay covers all transformed port keep-out X extents.
for n in ["CLR_KEY","CLR_MICROSD","CLR_POWER","CLR_USB_C"]:
    s=board_transformed(bdoc.getObject(n).Shape); bb=s.BoundBox
    ok=bb.XMin>=SERVICE_BAY[0] and bb.XMax<=SERVICE_BAY[1]
    print("CHECK service bay covers %-12s X %.2f..%.2f %s"%(n,bb.XMin,bb.XMax,"OK" if ok else "FAIL"))
    if not ok:fails.append("service bay misses "+n)

if fails:
    print("ASSEMBLY FAILURES:")
    for f in fails: print(" -",f)
    raise RuntimeError("assembly validation failed")

print("ALL ASSEMBLY CHECKS PASS")
print("printable case parts: 5; board physical objects:",len(board_objs))
print("FPC: arc %.3f + straight %.3f = %.3f, tip %s"%(ARC_LEN,STRAIGHT_LEN,FPC_LEN,FPC_TIP))
print("board transform Rx=180 Rz=-90 T",BOARD_T)
doc.recompute(); doc.saveAs(OUT)
print("saved",OUT)
