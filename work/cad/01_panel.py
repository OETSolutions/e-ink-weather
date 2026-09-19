# GDEH0576T81 e-paper panel, landscape. Origin = lower-left of outline, Z up from the glass BACK.
import FreeCAD as App
import Part
import os
import math

OUT = os.path.dirname(os.path.abspath(__file__))

W, H, T = 125.4, 99.5, 0.9            # SRC p4 125.4(H) x 99.5(V) x 0.9(D)
R_CORNER = 2.0                         # ASSUME corner radius (drawing shows a small radius)
ACT_W, ACT_H = 117.668, 86.972         # SRC p4/p5 117.668(H) x 86.972(V)
BORDER_W, BORDER_H = 118.87, 88.17     # SRC p5 BORDER 118.87 x 88.17

# Border asymmetry - SRC p5 drawing callouts. The active area is NOT centred: the bottom
# edge (where the FPC exits) carries a much fatter border, so the AA is pushed upward.
#   top 2.63 (to AA) / 2.03 (to border)
#   sides 3.87 / 3.27  (symmetric left/right)
#   bottom 9.90 / 9.30
# Cross-checks: 2.63 + 86.972 + 9.898 = 99.50  and  3.87 + 117.668 + 3.87 = 125.408
MARGIN_SIDE_AA = 3.87                  # SRC p5
MARGIN_TOP_AA = 2.63                   # SRC p5
MARGIN_BOT_AA = 9.90                   # SRC p5 TFT OD to TFT AA (bottom)
MARGIN_SIDE_BD = 3.27                  # SRC p5
MARGIN_TOP_BD = 2.03                   # SRC p5
MARGIN_BOT_BD = 9.30                   # SRC p5

# FPC tail, from GDEH0576T81_display.pdf p5 (printed 4/17), measured off the vector drawing
# at 6.33 px/mm calibrated on the 24+-0.3 dimension line:
#   24+-0.3    = tail LENGTH, panel edge -> tip
#   12.50+-0.10= tail WIDTH at the tip
#   12.00+-0.08= pin-array width (24 pins x 0.50 pitch)
#   3.69+-0.2  = taper length; the tail is ~24.5 wide where it leaves the panel
#                and narrows to 12.5 over that taper (this is the mistake that was made
#                before: the BASE width was used as the tip width, and length was invented)
TAB_W_ROOT = 24.5     # width where the tail meets the panel edge
TAB_W_TIP = 12.5      # width at the tip (drawing: 12.50+-0.10)
TAIL_LEN = 24.0       # drawing: 24+-0.3
TAPER_LEN = 3.69      # drawing: 3.69+-0.2
STRAIGHT_LEN = TAIL_LEN - TAPER_LEN   # 20.31
STIFF_LEN = 6.0       # PI stiffener, from the drawing's back-side detail
STIFF_T = 0.25
FPC_T = 0.12


def tapered_tail_wire(w_root, w_tip, taper, straight):
    """FPC outline: straight run at w_tip, then a flare out to w_root. Origin at the tip,
    +Y runs toward the panel. Returns a CCW wire."""
    hw_t, hw_r = w_tip / 2.0, w_root / 2.0
    P = lambda x, y: App.Vector(x, y, 0)
    return Part.Wire([
        Part.makeLine(P(-hw_t, 0), P(hw_t, 0)),                       # tip edge
        Part.makeLine(P(hw_t, 0), P(hw_t, straight)),                 # right straight
        Part.makeLine(P(hw_t, straight), P(hw_r, straight + taper)),  # right flare
        Part.makeLine(P(hw_r, straight + taper), P(-hw_r, straight + taper)),  # panel edge
        Part.makeLine(P(-hw_r, straight + taper), P(-hw_t, straight)),         # left flare
        Part.makeLine(P(-hw_t, straight), P(-hw_t, 0)),               # left straight
    ])


def rounded_rect_wire(w, h, r):
    """CCW rounded-rectangle wire in the XY plane, origin at lower-left."""
    r = min(r, w / 2.0 - 1e-6, h / 2.0 - 1e-6)
    def P(x, y):
        return App.Vector(x, y, 0)
    edges = [
        Part.makeLine(P(r, 0), P(w - r, 0)),
        Part.ArcOfCircle(Part.Circle(P(w - r, r), App.Vector(0, 0, 1), r),  # right-bottom
                         math.radians(270), math.radians(360)).toShape(),
        Part.makeLine(P(w, r), P(w, h - r)),
        Part.ArcOfCircle(Part.Circle(P(w - r, h - r), App.Vector(0, 0, 1), r),  # right-top
                         math.radians(0), math.radians(90)).toShape(),
        Part.makeLine(P(w - r, h), P(r, h)),
        Part.ArcOfCircle(Part.Circle(P(r, h - r), App.Vector(0, 0, 1), r),  # left-top
                         math.radians(90), math.radians(180)).toShape(),
        Part.makeLine(P(0, h - r), P(0, r)),
        Part.ArcOfCircle(Part.Circle(P(r, r), App.Vector(0, 0, 1), r),  # left-bottom
                         math.radians(180), math.radians(270)).toShape(),
    ]
    return Part.Wire(edges)


doc = App.newDocument("Panel")

# --- glass body
glass_face = Part.Face(rounded_rect_wire(W, H, R_CORNER))
glass = doc.addObject("Part::Feature", "PANEL_GLASS")
glass.Shape = glass_face.extrude(App.Vector(0, 0, T))

# --- FPC tail, exits the BOTTOM long edge (Y=0), centred
tail_face = Part.Face(tapered_tail_wire(TAB_W_ROOT, TAB_W_TIP, TAPER_LEN, STRAIGHT_LEN))
tail_face.translate(App.Vector(W / 2.0, -TAIL_LEN, 0.0))   # origin at tail tip
fpc = doc.addObject("Part::Feature", "PANEL_FPC_TAIL")
fpc.Shape = tail_face.extrude(App.Vector(0, 0, FPC_T))

# --- PI stiffener over the gold-finger end of the tail
stiff_face = Part.makePlane(TAB_W_TIP, STIFF_LEN,
                            App.Vector(W / 2.0 - TAB_W_TIP / 2.0, -TAIL_LEN, FPC_T))
stiff = doc.addObject("Part::Feature", "PANEL_FPC_STIFFENER")
stiff.Shape = stiff_face.extrude(App.Vector(0, 0, STIFF_T))


def marker(name, w, h, z, m_side, m_bot):
    """Rectangular marker offset by its own margins from the outline (SRC p5 callouts)."""
    f = Part.makePlane(w, h, App.Vector(m_side, m_bot, z))
    o = doc.addObject("Part::Feature", name)
    o.Shape = f
    return o


marker("PANEL_ACTIVE_AREA", ACT_W, ACT_H, T + 0.001, MARGIN_SIDE_AA, MARGIN_BOT_AA)
marker("PANEL_BORDER", BORDER_W, BORDER_H, T + 0.002, MARGIN_SIDE_BD, MARGIN_BOT_BD)

doc.recompute()
doc.saveAs(os.path.join(OUT, "panel.FCStd"))

bb = glass.Shape.BoundBox
print("PANEL bbox X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f" %
      (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
print("glass volume %.1f mm^3 (expect %.1f)" % (glass.Shape.Volume, W * H * T))
print("faces:", len(glass.Shape.Faces), " solids:", len(glass.Shape.Solids))

fb = fpc.Shape.BoundBox
print("FPC  bbox X %.2f..%.2f  Y %.2f..%.2f (want 24.5 root / 12.5 tip, len 24.0)" %
      (fb.XMin, fb.XMax, fb.YMin, fb.YMax))
print("FPC  XLength %.2f (root width, want 24.5)   YLength %.2f (length, want 24.0)" %
      (fb.XLength, fb.YLength))
sb = stiff.Shape.BoundBox
print("STIFF XLength %.2f (tip width, want 12.5)" % sb.XLength)
print("FPC volume %.1f mm^3 (expect %.1f)" %
      (fpc.Shape.Volume, (TAB_W_TIP * STRAIGHT_LEN + 0.5 * (TAB_W_ROOT + TAB_W_TIP) * TAPER_LEN) * FPC_T))

# --- border closure identities (SRC p5). These must close; if they don't, a margin is wrong.
for nm, got, want in [
    ("AA  H: 2.63+86.972+9.90", MARGIN_TOP_AA + ACT_H + MARGIN_BOT_AA, H),
    ("AA  W: 3.87+117.668+3.87", 2 * MARGIN_SIDE_AA + ACT_W, W),
    ("BD  H: 2.03+88.17+9.30", MARGIN_TOP_BD + BORDER_H + MARGIN_BOT_BD, H),
    ("BD  W: 3.27+118.87+3.27", 2 * MARGIN_SIDE_BD + BORDER_W, W),
]:
    flag = "OK" if abs(got - want) <= 0.05 else "**FAIL**"
    print("  closure %-26s %8.3f vs %8.3f  %s" % (nm, got, want, flag))
print("saved panel.FCStd")
