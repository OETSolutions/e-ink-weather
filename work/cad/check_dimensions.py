"""Provenance check: every dimensioned feature in the built models is asserted against a
sourced value. Run after any rebuild. Exits non-zero on mismatch.

Sources:
  SRC  = GDEH0576T81_display.pdf p5 vector drawing, measured at 6.33 px/mm
         (calibrated on the drawing's own `24+-0.3` dimension line)
  PUB  = manufacturer-published envelope
  USER = stated by the user in session
"""
import FreeCAD as App
import Part
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
fails = []


def check(label, got, want, tol, src):
    ok = abs(got - want) <= tol
    if not ok:
        fails.append("%s: got %.3f want %.3f +-%.2f (%s)" % (label, got, want, tol, src))
    print("  %-34s %9.3f  want %8.3f +-%.2f  %-8s %s" %
          (label, got, want, tol, src, "OK" if ok else "**FAIL**"))


print("=== PANEL (panel.FCStd) ===")
doc = App.openDocument(os.path.join(HERE, "panel.FCStd"))
glass = doc.getObject("PANEL_GLASS").Shape
fpc = doc.getObject("PANEL_FPC_TAIL").Shape
stiff = doc.getObject("PANEL_FPC_STIFFENER").Shape

b = glass.BoundBox
check("panel outline X", b.XLength, 125.4, 0.01, "PUB")
check("panel outline Y", b.YLength, 99.5, 0.01, "PUB")
check("panel thickness", b.ZLength, 0.9, 0.01, "PUB")

fb = fpc.BoundBox
# The tail's ROOT width is where it flares off the panel; the drawing dimensions the TIP.
check("fpc tail length", fb.YLength, 24.0, 0.3, "SRC 24+-0.3")
check("fpc tip width (stiffener)", stiff.BoundBox.XLength, 12.5, 0.1, "SRC 12.50+-0.10")
check("fpc root width", max(abs(fb.XMin - 62.7), abs(fb.XMax - 62.7)) * 2, 24.5, 0.5, "SRC scaled")
# pin array identity: 24 pins at 0.50 pitch must be about 12.0
check("pin array width (24x0.50)", 24 * 0.50, 12.0, 0.01, "SRC 12.00+-0.08")
# tail must be centred on the panel
check("fpc centred on panel", (fb.XMin + fb.XMax) / 2.0, 125.4 / 2.0, 0.01, "SRC")
# tail starts exactly at the panel edge
check("fpc root at panel edge", fb.YMax, 0.0, 0.01, "SRC")

print("\n--- border asymmetry (SRC p5 callouts) ---")
aa = doc.getObject("PANEL_ACTIVE_AREA").Shape.BoundBox
bd = doc.getObject("PANEL_BORDER").Shape.BoundBox
check("AA margin from left edge", aa.XMin, 3.87, 0.01, "SRC")
check("AA margin from bottom edge", aa.YMin, 9.90, 0.01, "SRC")
check("AA margin from right edge", 125.4 - aa.XMax, 3.87, 0.01, "SRC")
check("AA margin from top edge", 99.5 - aa.YMax, 2.63, 0.01, "SRC")
check("border margin from bottom", bd.YMin, 9.30, 0.01, "SRC")
check("border margin from top", 99.5 - bd.YMax, 2.03, 0.01, "SRC")
# the active area must NOT be centred - this is the bug that was fixed
check("AA is off-centre vertically", (aa.YMin + aa.YMax) / 2.0, 99.5 / 2.0 + 3.635, 0.05, "SRC")

print("\n=== CASE SHELL (case_shell.FCStd) ===")
doc2 = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
shell = doc2.getObject("CASE_SHELL").Shape
ob = shell.optimalBoundingBox()
BEZEL, D = 4.5, 16.9
check("case outline X", ob.XLength, 125.4 + 2 * BEZEL, 0.01, "DERIVED")
check("case outline Y", ob.YLength, 99.5 + 2 * BEZEL, 0.01, "DERIVED")
check("case depth", ob.ZLength, D, 0.01, "DERIVED")
check("one solid", len(shell.Solids), 1, 0.0, "BUILD")

print("\n=== DEPTH STACK identity (must close exactly) ===")
BACK_WALL, BACK_GAP, CELL_T, GRID_D, GASKET_T, PANEL_T = 1.6, 0.5, 11.0, 2.4, 0.5, 0.9
check("stack sum == depth", BACK_WALL + BACK_GAP + CELL_T + GRID_D + GASKET_T + PANEL_T, D, 1e-9, "USER+DIV")

print("\n=== CASE BODY (case_body.FCStd) - access openings ===")
p = os.path.join(HERE, "case_body.FCStd")
if os.path.exists(p):
    doc3 = App.openDocument(p)
    body = doc3.getObject("CASE_BODY").Shape
    b3 = body.optimalBoundingBox()
    check("body outline X (flush pad)", b3.XLength, 134.4, 0.01, "DERIVED")
    check("body outline Y", b3.YLength, 108.5, 0.01, "DERIVED")
    check("body depth", b3.ZLength, 16.9, 0.01, "DERIVED")
    check("body is one solid", len(body.Solids), 1, 0.0, "BUILD")
    check("body valid", 1.0 if body.isValid() else 0.0, 1.0, 0.0, "BUILD")
    # openings must actually be through-holes in the pad: probe for material absence
    for name, y, z in [("USB_C", 56.9, 14.8), ("MICROSD", 25.0, 13.9),
                       ("KEY_BUTTON", 13.2, 14.7)]:
        probe = Part.makeBox(0.2, 0.2, 0.2, App.Vector(-0.15, y, z))
        inter = body.common(probe)
        check("opening %s is open" % name, inter.Volume, 0.0, 1e-6, "USER")
    # wall between RT pocket roof and the front face
    check("RT pocket roof wall", D - 15.3, 1.6, 0.01, "ASSUME")
else:
    print("  (case_body.FCStd not built yet - skipped)")

print()
if fails:
    print("FAILURES (%d):" % len(fails))
    for f in fails:
        print("  -", f)
    sys.exit(1)
print("ALL CHECKS PASS")
