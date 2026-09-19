# Case outer shell for the e-ink weather display, landscape.
# Origin = lower-left of the CASE outline. Z=0 is the rear face, Z=D the front face.
import FreeCAD as App
import Part
import os
import sys
import math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import (PANEL_W,PANEL_H,PANEL_T,CASE_W,CASE_H,CASE_D,
                                  REAR_COVER_T,BATTERY_REAR_PAD,BATTERY_T,GRID_T,
                                  REAR_FOAM_T,FRONT_GASKET_T,RETAINING_LIP_T,
                                  BATTERY_Z0,BATTERY_Z1,GRID_Z0,GRID_Z1,PANEL_Z,
                                  PANEL_Z1,LIP_UNDERSIDE)

OUT = os.path.dirname(os.path.abspath(__file__))

# ---- exterior styling ------------------------------------------------------
BEZEL = 4.5          # visible bezel: glass edge -> case edge
FRONT_ROLL_R = 3.4   # front edge radius (the "seems slimmer" feature)
BACK_CHAMFER = 8.0   # USER requested reduced rear footprint
                     # 19.0 - 8.0 - 3.4 = 7.6 mm straight side remains in profile.
CORNER_R = 9.0
D = CASE_D
Z_FRONT = CASE_D


def rounded_rect_wire(w, h, r, cx=0.0, cy=0.0):
    """CCW rounded-rectangle wire in XY. (cx,cy) = lower-left corner."""
    r = min(r, w / 2.0 - 1e-6, h / 2.0 - 1e-6)
    def P(x, y):
        return App.Vector(cx + x, cy + y, 0)
    return Part.Wire([
        Part.makeLine(P(r, 0), P(w - r, 0)),
        Part.ArcOfCircle(Part.Circle(P(w - r, r), App.Vector(0, 0, 1), r), math.radians(270), math.radians(360)).toShape(),
        Part.makeLine(P(w, r), P(w, h - r)),
        Part.ArcOfCircle(Part.Circle(P(w - r, h - r), App.Vector(0, 0, 1), r), math.radians(0), math.radians(90)).toShape(),
        Part.makeLine(P(w - r, h), P(r, h)),
        Part.ArcOfCircle(Part.Circle(P(r, h - r), App.Vector(0, 0, 1), r), math.radians(90), math.radians(180)).toShape(),
        Part.makeLine(P(0, h - r), P(0, r)),
        Part.ArcOfCircle(Part.Circle(P(r, r), App.Vector(0, 0, 1), r), math.radians(180), math.radians(270)).toShape(),
    ])


doc = App.newDocument("CaseShell")

# Build the rear taper from two *matching rounded-rectangle sections*, not OCC's edge
# chamfer operator. The chamfer operator generated triangular transition faces where each
# straight chamfer met a rounded corner. A ruled loft gives eight corresponding faces
# (four straight + four curved corner faces) with no triangular end patches.
rear_inset = BACK_CHAMFER
rear_r = max(1.0, CORNER_R - rear_inset)
rear_wire = rounded_rect_wire(CASE_W - 2*rear_inset, CASE_H - 2*rear_inset,
                              rear_r, rear_inset, rear_inset)
rear_wire.translate(App.Vector(0, 0, 0.0))
full_wire = rounded_rect_wire(CASE_W, CASE_H, CORNER_R)
full_wire.translate(App.Vector(0, 0, BACK_CHAMFER))
rear_taper = Part.makeLoft([rear_wire, full_wire], True, True)  # solid, ruled
upper = Part.Face(rounded_rect_wire(CASE_W, CASE_H, CORNER_R)).extrude(
    App.Vector(0, 0, (D - FRONT_ROLL_R) - BACK_CHAMFER))
upper.translate(App.Vector(0, 0, BACK_CHAMFER))
body = rear_taper.fuse(upper).removeSplitter()
print("ruled taper: faces %d solids %d valid %s" %
      (len(body.Faces), len(body.Solids), body.isValid()))

tol = 1e-4


def edges_in_plane(shape, z):
    out = []
    for e in shape.Edges:
        bb = e.BoundBox
        if abs(bb.ZMin - z) < tol and abs(bb.ZMax - z) < tol:
            out.append(e)
    return out


# ---- front roll: built from matched rounded-rectangle sections ----------------
# The roll is generated as a stack of ruled lofts between rounded-rectangle sections whose
# inset follows a true circular arc, then capped by the front face. This replaces
# `makeFillet` on the front rim: that operator pulled the four corner junctions 0.74 mm
# outside the 134.4 mm outline (toroidal corner surfaces spanning z 15.6..19.0), i.e. the
# case grew beyond its own locked footprint. Ruled lofts between matched sections cannot do
# that, and this is the same technique already proven on the rear taper.
N_ROLL = 10
roll_stations = []
for i in range(N_ROLL + 1):
    t = i / float(N_ROLL)
    z = (D - FRONT_ROLL_R) + t * FRONT_ROLL_R
    # Inset from the full outline: 0 at the roll start, growing to R at the front face.
    inset = FRONT_ROLL_R - math.sqrt(max(FRONT_ROLL_R ** 2 - (t * FRONT_ROLL_R) ** 2, 0))
    roll_stations.append((z, inset))
roll_sections = []
for z, inset in roll_stations:
    w = CASE_W - 2 * inset
    h = CASE_H - 2 * inset
    r = max(0.5, CORNER_R - inset)
    roll_sections.append(rounded_rect_wire(w, h, r, inset, inset))
roll_sections[0].translate(App.Vector(0, 0, roll_stations[0][0]))
for i in range(1, len(roll_sections)):
    roll_sections[i].translate(App.Vector(0, 0, roll_stations[i][0]))
roll_parts = [Part.makeLoft([roll_sections[i], roll_sections[i + 1]], True, True)
              for i in range(len(roll_sections) - 1)]
roll = roll_parts[0]
for p in roll_parts[1:]:
    roll = roll.fuse(p)
body = body.fuse(roll).removeSplitter()
# Cap the front. The roll's last section is a ring inset by R, so the front face is the
# smaller rounded rectangle it terminates on -- not the full outline. Capping the full
# outline here would re-close the case and undo the roll.
front_w = CASE_W - 2 * FRONT_ROLL_R
front_h = CASE_H - 2 * FRONT_ROLL_R
front_r = max(0.5, CORNER_R - FRONT_ROLL_R)
front_cap = Part.Face(rounded_rect_wire(front_w, front_h, front_r,
                                        FRONT_ROLL_R, FRONT_ROLL_R)).extrude(App.Vector(0, 0, 0.001))
front_cap.translate(App.Vector(0, 0, D - 0.001))
body = body.fuse(front_cap).removeSplitter()
print("after generated front roll: valid %s faces %d" % (body.isValid(), len(body.Faces)))
if body.BoundBox.XMax > CASE_W + 1e-3 or body.BoundBox.YMax > CASE_H + 1e-3:
    raise RuntimeError("front roll exceeds the %s x %s outline" % (CASE_W, CASE_H))
if body.BoundBox.XMin < -1e-3 or body.BoundBox.YMin < -1e-3:
    raise RuntimeError("front roll undershoots the case origin")

# The rear taper is already the requested 8 mm four-sided chamfer. No edge chamfer is
# applied here: doing so is what caused the unwanted triangular corner transition faces.

shell = doc.addObject("Part::Feature", "CASE_SHELL")
shell.Shape = body
doc.recompute()
doc.saveAs(os.path.join(OUT, "case_shell.FCStd"))

bb = body.BoundBox
print("SHELL bbox X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f" %
      (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

ob = body.optimalBoundingBox()
print("SHELL optimal bbox X %.3f..%.3f  Y %.3f..%.3f  Z %.3f..%.3f" %
      (ob.XMin, ob.XMax, ob.YMin, ob.YMax, ob.ZMin, ob.ZMax))

# cross-section width vs height: proves the front roll + rear roll are real
print("   z      width    expect")
for z in [0.0, 0.6, 1.2, 5.0, 13.4, 13.5, 14.0, 15.0, 16.0, 16.85, 16.89]:
    c = body.common(Part.makeBox(400, 400, 0.01, App.Vector(-100, -100, z)))
    w = c.BoundBox.XLength
    if z <= BACK_CHAMFER:
        f = BACK_CHAMFER - z
    elif z >= D - FRONT_ROLL_R:
        t = z - (D - FRONT_ROLL_R)
        f = FRONT_ROLL_R - math.sqrt(max(FRONT_ROLL_R ** 2 - t ** 2, 0))
    else:
        f = 0.0
    e = CASE_W - 2 * f
    print("%7.2f %8.3f %8.3f" % (z, w, e))
print("CASE_W %.1f  CASE_H %.1f  D %.1f" % (CASE_W, CASE_H, D))
print("z planes: cover %.1f cell %.1f..%.1f grid %.1f..%.1f glass %.1f..%.1f lip %.1f..%.1f" %
      (REAR_COVER_T, BATTERY_Z0, BATTERY_Z1, GRID_Z0, GRID_Z1,
       PANEL_Z, PANEL_Z1, LIP_UNDERSIDE, CASE_D))
print("volume %.1f mm^3 (solid prism would be %.1f)" % (body.Volume, CASE_W * CASE_H * D))
print("saved case_shell.FCStd")
