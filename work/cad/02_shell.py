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
CORNER_R = 9.0
D = CASE_D
Z_FRONT = CASE_D

# Front edge profile. The printed front bezel is placed COSMETIC FACE DOWN, so the front face
# (z=CASE_D) is the bed side and the case is built toward decreasing z. The previous profile
# was a full 3.4 mm circular roll; in that orientation the roll's outer edge leaves the bed
# vertical, so the outer ~1.1 mm of every one of the first layers printed in mid-air -- this is
# the "prints ugly" the user reported. A circular roll cannot be both flat on the bed and
# shallow-angled at the bed, so the roll is kept for its slim silhouette and only its
# overhanging bed edge is replaced by a lead chamfer at a true printable angle.
FRONT_ROLL_R = 3.4   # front edge radius (the "seems slimmer" feature)
LEAD_DEG = 45.0      # bed-edge lead chamfer angle from the bed; 45 is the usual FDM limit
# The roll's own tangent reaches LEAD_DEG at theta=LEAD_DEG; below that it is already
# printable, so the chamfer only has to span that last arc. Anything gentler cannot both
# preserve the 3.4 mm roll and clear the bezel's 2.3 mm fastener band and FPC exit.
_SIN, _COS = math.sin(math.radians(LEAD_DEG)), math.cos(math.radians(LEAD_DEG))
KNEE_Z = (D - FRONT_ROLL_R) + FRONT_ROLL_R * _SIN        # roll -> chamfer handover
KNEE_INSET = FRONT_ROLL_R * (1.0 - _COS)
LEAD_INSET = KNEE_INSET + (D - KNEE_Z)                   # inset at the front/bed face
BACK_CHAMFER = 8.0   # USER requested reduced rear footprint



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


# ---- front edge: circular roll + printable lead chamfer at the bed ----------------------
# Matched rounded-rectangle sections in ONE ruled loft (the technique already proven on the
# rear taper; makeFillet on the front rim pulled the corners 0.74 mm outside the 134.4 mm
# outline). Sections run from the top of the straight side, around the roll's printable arc,
# to the knee where the tangent reaches LEAD_DEG, then straight to the front/bed face. The
# ruled chamfer is a true lead-in: it sits inside the roll, so no station exceeds LEAD_DEG.
N_ROLL = 10
roll_stations = []
for i in range(N_ROLL + 1):
    t = i / float(N_ROLL)                      # 0 at the roll start, 1 at the knee
    th = t * math.radians(LEAD_DEG)
    z = (D - FRONT_ROLL_R) + FRONT_ROLL_R * math.sin(th)
    inset = FRONT_ROLL_R * (1.0 - math.cos(th))
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
face_sec = rounded_rect_wire(CASE_W - 2 * LEAD_INSET, CASE_H - 2 * LEAD_INSET,
                             max(0.5, CORNER_R - LEAD_INSET), LEAD_INSET, LEAD_INSET)
face_sec.translate(App.Vector(0, 0, D))
roll_sections.append(face_sec)
roll_parts = [Part.makeLoft([roll_sections[i], roll_sections[i + 1]], True, True)
              for i in range(len(roll_sections) - 1)]
roll = roll_parts[0]
for p in roll_parts[1:]:
    roll = roll.fuse(p)
body = body.fuse(roll).removeSplitter()
# Cap the front. The front section is a ring inset by LEAD_INSET, so the front face is the
# smaller rounded rectangle it terminates on -- not the full outline; capping the full outline
# would re-close the case and undo the chamfer.
front_w = CASE_W - 2 * LEAD_INSET
front_h = CASE_H - 2 * LEAD_INSET
front_r = max(0.5, CORNER_R - LEAD_INSET)
front_cap = Part.Face(rounded_rect_wire(front_w, front_h, front_r,
                                        LEAD_INSET, LEAD_INSET)).extrude(App.Vector(0, 0, 0.001))
front_cap.translate(App.Vector(0, 0, D - 0.001))
body = body.fuse(front_cap).removeSplitter()
print("front edge: roll to knee z=%.3f inset=%.3f, lead chamfer %.0f deg to inset %.3f" %
      (KNEE_Z, KNEE_INSET, LEAD_DEG, LEAD_INSET))
print("after front roll + lead chamfer: valid %s faces %d" % (body.isValid(), len(body.Faces)))
if body.BoundBox.XMax > CASE_W + 1e-3 or body.BoundBox.YMax > CASE_H + 1e-3:
    raise RuntimeError("front edge exceeds the %s x %s outline" % (CASE_W, CASE_H))
if body.BoundBox.XMin < -1e-3 or body.BoundBox.YMin < -1e-3:
    raise RuntimeError("front edge undershoots the case origin")

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

# cross-section width vs height: proves the rear taper, the roll and the lead chamfer are real
print("   z      width    expect   slope_from_bed")
def _inset(z):
    if z <= BACK_CHAMFER:
        return BACK_CHAMFER - z                      # rear taper, 45 deg
    if z >= D:
        return LEAD_INSET                            # front face
    if z >= KNEE_Z:
        return KNEE_INSET + (z - KNEE_Z)             # lead chamfer, ruled-linear
    t = z - (D - FRONT_ROLL_R)                       # roll
    if t <= 0.0:
        return 0.0
    return FRONT_ROLL_R - math.sqrt(max(FRONT_ROLL_R ** 2 - t ** 2, 0.0))
for z in [0.0, 0.6, 1.2, 5.0, KNEE_Z - 1.0, KNEE_Z - 0.2, KNEE_Z + 0.2,
          D - 0.5, D - 0.1]:
    c = body.common(Part.makeBox(400, 400, 0.01, App.Vector(-100, -100, z)))
    w = c.BoundBox.XLength
    e = CASE_W - 2 * _inset(z)
    dz = 0.05
    slope = math.degrees(math.atan(abs(_inset(z + dz) - _inset(z)) / dz))
    print("%7.2f %8.3f %8.3f %12.1f" % (z, w, e, slope))
print("CASE_W %.1f  CASE_H %.1f  D %.1f" % (CASE_W, CASE_H, D))
print("z planes: cover %.1f cell %.1f..%.1f grid %.1f..%.1f glass %.1f..%.1f lip %.1f..%.1f" %
      (REAR_COVER_T, BATTERY_Z0, BATTERY_Z1, GRID_Z0, GRID_Z1,
       PANEL_Z, PANEL_Z1, LIP_UNDERSIDE, CASE_D))
print("volume %.1f mm^3 (solid prism would be %.1f)" % (body.Volume, CASE_W * CASE_H * D))
print("saved case_shell.FCStd")
