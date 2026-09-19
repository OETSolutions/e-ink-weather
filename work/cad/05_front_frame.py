"""Printable front frame + display support lattice.

This is one FDM-printable solid. Print front-face-down. It contains:
- the rounded front perimeter and 3.4 mm front roll from case_shell.FCStd
- a flush pocket for the real 125.4 x 99.5 panel
- a connected open grid behind the full panel
- a bottom under-bezel channel for the 180-degree FPC fold

Dimensions are tagged SRC / USER / ASSUME. No unlabelled design dimensions.
"""
import FreeCAD as App
import Part
import os, math

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- sourced / locked envelope ------------------------------------------------
CASE_W, CASE_H, CASE_D = 134.4, 108.5, 16.9       # DERIVED panel + USER bezel/depth
PANEL_W, PANEL_H, PANEL_T = 125.4, 99.5, 0.9       # SRC datasheet p4/p5
PANEL_X, PANEL_Y, PANEL_Z = 4.5, 4.5, 15.7         # DERIVED: panel front z16.6, 0.3 below bezel face
SPLIT_Z = 13.1                                      # DERIVED top of USER 11 mm cell
GRID_BOTTOM, GRID_TOP = 14.0, 15.2                  # ASSUME 1.2 mm rib depth
FOAM_T = 0.5                                        # ASSUME closed-cell foam
GRID_RIB = 1.0                                      # ASSUME FDM rib width
GRID_CELL = 5.0                                     # ASSUME clear cell size (stated earlier)
GRID_PITCH = GRID_RIB + GRID_CELL
PANEL_CLEAR = 0.20                                  # ASSUME 0.10 mm per side FDM clearance
SEAT_WIDTH = 2.2                                    # ASSUME glass edge support width
FPC_ROOT_W = 24.5                                   # SRC p5 scaled root width
FPC_CHANNEL_CLEAR = 1.0                             # ASSUME 0.5 mm clearance each side
FPC_CHANNEL_Z0 = 11.4                               # DERIVED below folded bend
FPC_CHANNEL_Z1 = PANEL_Z + 0.15                     # ASSUME clearance over FPC root


def rounded_rect_wire(w, h, r, x=0.0, y=0.0, z=0.0):
    r = min(r, w / 2.0 - 1e-6, h / 2.0 - 1e-6)
    P = lambda a, b: App.Vector(x + a, y + b, z)
    return Part.Wire([
        Part.makeLine(P(r, 0), P(w-r, 0)),
        Part.ArcOfCircle(Part.Circle(P(w-r, r), App.Vector(0,0,1), r), math.radians(270), math.radians(360)).toShape(),
        Part.makeLine(P(w, r), P(w, h-r)),
        Part.ArcOfCircle(Part.Circle(P(w-r, h-r), App.Vector(0,0,1), r), math.radians(0), math.radians(90)).toShape(),
        Part.makeLine(P(w-r, h), P(r, h)),
        Part.ArcOfCircle(Part.Circle(P(r, h-r), App.Vector(0,0,1), r), math.radians(90), math.radians(180)).toShape(),
        Part.makeLine(P(0, h-r), P(0, r)),
        Part.ArcOfCircle(Part.Circle(P(r, r), App.Vector(0,0,1), r), math.radians(180), math.radians(270)).toShape(),
    ])


def rounded_prism(w, h, r, z0, dz, x=0.0, y=0.0):
    return Part.Face(rounded_rect_wire(w, h, r, x, y, z0)).extrude(App.Vector(0,0,dz))


# Source outer surface: exact verified fillet/rounded-corner geometry.
src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = src.getObject("CASE_SHELL").Shape
upper_clip = Part.makeBox(CASE_W + 20, CASE_H + 20, CASE_D - SPLIT_Z + 1,
                          App.Vector(-10, -10, SPLIT_Z))
upper = outer.common(upper_clip)

# Remove the panel footprint all the way through; printable material is then added back only
# as a perimeter seat and open lattice. Pocket includes 0.10 mm side clearance.
pocket = rounded_prism(PANEL_W + PANEL_CLEAR, PANEL_H + PANEL_CLEAR, 2.1,
                       SPLIT_Z - 0.1, CASE_D - SPLIT_Z + 1.2,
                       PANEL_X - PANEL_CLEAR/2, PANEL_Y - PANEL_CLEAR/2)
frame = upper.cut(pocket)

# 2.2 mm-wide seat ring under the panel perimeter, top at z=15.5 so 0.5 mm foam closes to z=16.
# The hidden seat extends 0.20 mm OUTSIDE the pocket boundary to overlap/fuse into the
# perimeter. Panel clearance applies only above z=15.5; the support below must be structural.
SEAT_OVERLAP = 0.20                                  # ASSUME boolean/FDM structural overlap
seat_outer = rounded_prism(PANEL_W + PANEL_CLEAR + 2*SEAT_OVERLAP,
                           PANEL_H + PANEL_CLEAR + 2*SEAT_OVERLAP, 2.3,
                           GRID_BOTTOM, GRID_TOP-GRID_BOTTOM,
                           PANEL_X - PANEL_CLEAR/2 - SEAT_OVERLAP,
                           PANEL_Y - PANEL_CLEAR/2 - SEAT_OVERLAP)
seat_inner = rounded_prism(PANEL_W-2*SEAT_WIDTH, PANEL_H-2*SEAT_WIDTH,
                           max(0.5, 2.0-SEAT_WIDTH), GRID_BOTTOM-0.1,
                           GRID_TOP-GRID_BOTTOM+0.2,
                           PANEL_X+SEAT_WIDTH, PANEL_Y+SEAT_WIDTH)
seat = seat_outer.cut(seat_inner)

# Open 5 x 5 mm clear-cell grid, 1.0 mm ribs, 1.2 mm deep. Ribs terminate into seat ring.
ribs = []
x = PANEL_X + SEAT_WIDTH + GRID_CELL
while x < PANEL_X + PANEL_W - SEAT_WIDTH:
    ribs.append(Part.makeBox(GRID_RIB, PANEL_H-2*SEAT_WIDTH, GRID_TOP-GRID_BOTTOM,
                             App.Vector(x-GRID_RIB/2, PANEL_Y+SEAT_WIDTH, GRID_BOTTOM)))
    x += GRID_PITCH
y = PANEL_Y + SEAT_WIDTH + GRID_CELL
while y < PANEL_Y + PANEL_H - SEAT_WIDTH:
    ribs.append(Part.makeBox(PANEL_W-2*SEAT_WIDTH, GRID_RIB, GRID_TOP-GRID_BOTTOM,
                             App.Vector(PANEL_X+SEAT_WIDTH, y-GRID_RIB/2, GRID_BOTTOM)))
    y += GRID_PITCH
grid = Part.makeCompound(ribs)
carrier = seat.fuse(grid)
frame = frame.fuse(carrier)

# Under-bezel channel for the panel FPC root and the 180-degree bend. It stays behind the
# 0.9 mm front skin (z 16.0..16.9), so the visible bottom bezel remains continuous.
fpc_x0 = PANEL_X + PANEL_W/2 - (FPC_ROOT_W + FPC_CHANNEL_CLEAR)/2
fpc_cut = Part.makeBox(FPC_ROOT_W + FPC_CHANNEL_CLEAR,
                       PANEL_Y + 0.8,
                       FPC_CHANNEL_Z1 - FPC_CHANNEL_Z0,
                       App.Vector(fpc_x0, 0.0, FPC_CHANNEL_Z0))
frame = frame.cut(fpc_cut)

if len(frame.Solids) != 1 or not frame.isValid():
    raise RuntimeError("front frame is not one valid printable solid")

# Foam is non-printed, modelled separately as a full backing sheet to spread grid loads.
foam = rounded_prism(PANEL_W, PANEL_H, 2.0, GRID_TOP, FOAM_T, PANEL_X, PANEL_Y)
# Remove FPC exit strip from foam.
foam = foam.cut(Part.makeBox(FPC_ROOT_W+1.0, 12.0, FOAM_T+0.2,
                             App.Vector(fpc_x0, PANEL_Y-0.1, GRID_TOP-0.1)))

doc = App.newDocument("FrontFrame")
o = doc.addObject("Part::Feature", "PRINT_FRONT_FRAME")
o.Label = "PRINT: Front frame + support grid"
o.Shape = frame
o.addProperty("App::PropertyString", "PrintOrientation").PrintOrientation = "Front face down; no supports"
o.addProperty("App::PropertyString", "Material").Material = "PETG"
o.addProperty("App::PropertyString", "Assumptions").Assumptions = "1.0 mm ribs, 5 mm clear cells, 0.20 mm panel XY clearance"

f = doc.addObject("Part::Feature", "FOAM_BACKING_0_5MM")
f.Label = "CUT: 0.5 mm closed-cell foam backing"
f.Shape = foam
f.addProperty("App::PropertyString", "Material").Material = "0.5 mm closed-cell foam; not printed"

doc.recompute()
doc.saveAs(os.path.join(HERE, "front_frame.FCStd"))

b = frame.optimalBoundingBox()
print("FRONT_FRAME bbox %.2f x %.2f x %.2f  z %.2f..%.2f" %
      (b.XLength,b.YLength,b.ZLength,b.ZMin,b.ZMax))
print("solid %d valid %s volume %.0f" % (len(frame.Solids),frame.isValid(),frame.Volume))
print("grid ribs",len(ribs),"pitch",GRID_PITCH,"clear cell",GRID_CELL)
print("panel pocket %.2f x %.2f, clearance %.2f total" %
      (PANEL_W+PANEL_CLEAR,PANEL_H+PANEL_CLEAR,PANEL_CLEAR))
print("FPC channel x %.2f..%.2f y 0..%.2f z %.2f..%.2f" %
      (fpc_x0,fpc_x0+FPC_ROOT_W+FPC_CHANNEL_CLEAR,PANEL_Y+0.8,FPC_CHANNEL_Z0,FPC_CHANNEL_Z1))
print("saved front_frame.FCStd")
