"""Left-edge access: a flat pad (so openings don't break out onto the front roll) with
USB-C, microSD, power-slide and KEY openings, plus the recessed RT trimmer.

USER: "make flat faces for the slide switches, usb connector and sd card slot, looks like
only on the edge where the sd card, usb, power and key pushbutton are (the other two slide
switches don't need access, they're set one time)."

Board feature positions come from GoodDisplay_ESP32_M1_reconstructed_v2/..._locations_v2.csv
(photo-scaled, 0.3-0.8 mm XY uncertainty), board placed with its lower-left at (4.5, 4.5).
"""
import FreeCAD as App
import Part
import os, math

HERE = os.path.dirname(os.path.abspath(__file__))
doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
shell = doc.getObject("CASE_SHELL").Shape

D = 16.9
CASE_X0 = 0.0
BOARD_X, BOARD_Y = 4.5, 4.5

# --- flat pad on the left edge -------------------------------------------------
# Flush with the case's widest point (x=0), NOT protruding: the pad simply fills in the
# front-roll inset over this span, giving openings a flat land to sit on without growing
# the case footprint. The roll is interrupted only across the pad's 53.5 mm.
PAD_OUT_X = 0.0       # pad outer face - flush with the case outline
PAD_IN_X = 4.0        # pad inner face (stops short of the board at x=4.5)
PAD_Y0, PAD_Y1 = 9.4, 62.9
PAD_Z0, PAD_Z1 = 11.0, 16.3

pad = Part.makeBox(PAD_IN_X - PAD_OUT_X, PAD_Y1 - PAD_Y0, PAD_Z1 - PAD_Z0,
                   App.Vector(PAD_OUT_X, PAD_Y0, PAD_Z0))
body = shell.fuse(pad)
print("fused: solids %d valid %s vol %.0f" % (len(body.Solids), body.isValid(), body.Volume))

# The pad is flush with the case outline, so its outer face merges into the case surface and
# there is no protruding edge to round - the front roll is simply interrupted over the pad's
# span. Nothing to fillet here; the roll above the pad resumes at z = PAD_Z1.

# --- openings ------------------------------------------------------------------
# (name, y0, y1, z0, z1) in case coordinates
CUT_X0, CUT_X1 = PAD_OUT_X - 1.0, PAD_IN_X + 1.0
OPENINGS = [
    # USB-C: plug is 8.4 x 2.6; receptacle on board spans y 52.5..61.4, z 13.1..16.25
    ("USB_C",        52.9, 61.0, 13.5, 16.1),
    # microSD: card 15x11x1 inserts -X; socket on board y 17.25..32.65, just above PCB
    ("MICROSD",      18.2, 31.8, 13.15, 14.7),
    # power slide: notch open at the top so the actuator is reachable from the front too
    ("POWER_SLIDE",  34.4, 41.1, 14.6, 20.0),
    # KEY side button: board y 10.9..15.6, z 13.1..15.3
    ("KEY_BUTTON",   11.6, 14.9, 13.6, 15.9),
]
for name, y0, y1, z0, z1 in OPENINGS:
    b = Part.makeBox(CUT_X1 - CUT_X0, y1 - y0, z1 - z0, App.Vector(CUT_X0, y0, z0))
    body = body.cut(b)
    print("  cut %-14s y %5.1f..%5.1f  z %5.1f..%5.1f" % (name, y0, y1, z0, z1))

print("after cuts: solids %d valid %s vol %.0f" % (len(body.Solids), body.isValid(), body.Volume))

# --- RT brightness trimmer: recessed, not open ---------------------------------
# board (x 6.05, y 2.85) r1.9, z 13.1..15.35. Pocket reached from the BOTTOM face so the
# back silhouette is preserved. 1.6 mm wall left between pocket roof and the front face.
RT_POCKET_R = 2.2
rt_cx, rt_cy, rt_z0, rt_z1 = BOARD_X + 6.05, BOARD_Y + 2.85, 12.0, D - 1.6
cyl = Part.makeCylinder(RT_POCKET_R, rt_z1 - rt_z0, App.Vector(rt_cx, rt_cy, rt_z0))
body = body.cut(cyl)
print("  cut RT_TRimmer pocket r%.1f at (%.1f, %.1f) z %.1f..%.1f" %
      (RT_POCKET_R, rt_cx, rt_cy, rt_z0, rt_z1))

print("FINAL: solids %d valid %s vol %.0f" % (len(body.Solids), body.isValid(), body.Volume))

out = doc.addObject("Part::Feature", "CASE_BODY")
out.Shape = body
doc.recompute()
doc.saveAs(os.path.join(HERE, "case_body.FCStd"))
ob = body.optimalBoundingBox()
print("CASE_BODY optimal bbox X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f" %
      (ob.XMin, ob.XMax, ob.YMin, ob.YMax, ob.ZMin, ob.ZMax))
print("saved case_body.FCStd")
