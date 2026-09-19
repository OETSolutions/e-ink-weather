"""Validate the front bezel's side of the front-driven fastener.

The bezel no longer has blind pilots (those were unreachable by any driver). It now provides a
spotfaced counterbore and an open through-hole on the boss axis. This validator checks the
bezel side only; `26_validate_fastener_chain.py` checks the full chain including the chassis
boss and nut pocket, and `10_assembly.py` checks it in the assembled model.
"""
import FreeCAD as App, Part, os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from enclosure_dimensions import CASE_D, SPLIT_Z, LIP_UNDERSIDE
from functools import partial
print = partial(print, flush=True)
HERE = os.path.dirname(os.path.abspath(__file__))
d = App.openDocument(os.path.join(HERE, "EInk_Weather_Display_Assembly.FCStd"))
b = d.getObject("PRINT_FRONT_BEZEL").Shape
PTS = [(2.3, 32.0), (2.3, 76.5), (132.1, 32.0), (132.1, 76.5)]
HEAD_CBORE_D = 4.4
HEAD_CBORE_DEPTH = 1.2
CBORE_FLOOR = CASE_D - HEAD_CBORE_DEPTH
fails = []
print("=== FRONT BEZEL COUNTERBORE / THROUGH-HOLE VALIDATION ===")
for x, y in PTS:
    # Counterbore open across its width and through the head depth.
    cb = Part.makeCylinder(HEAD_CBORE_D/2 - 0.15, HEAD_CBORE_DEPTH - 0.15,
                           App.Vector(x, y, CBORE_FLOOR + 0.05))
    cb_open = 1.0 - (b.common(cb).Volume / cb.Volume)
    # Through-hole open from the split plane to the counterbore floor.
    th = Part.makeCylinder(1.0, CBORE_FLOOR - SPLIT_Z - 0.2,
                           App.Vector(x, y, SPLIT_Z + 0.1))
    th_open = 1.0 - (b.common(th).Volume / th.Volume)
    # Thread-bearing wall around the through-hole must be >= 1.2 mm (2.2 hole, boss r 1.9).
    ring = Part.makeCylinder(2.4, 1.0, App.Vector(x, y, SPLIT_Z + 0.5)).cut(
           Part.makeCylinder(1.1, 1.4, App.Vector(x, y, SPLIT_Z + 0.3)))
    wall = b.common(ring).Volume
    ok = cb_open > 0.98 and th_open > 0.98 and wall > 8.0
    print("  (%.1f,%.1f) counterbore %.2f through-hole %.2f wall %.1f mm3 %s" %
          (x, y, cb_open, th_open, wall, "OK" if ok else "FAIL"))
    if not ok:
        fails.append("bezel fastener feature failed at %.1f,%.1f" % (x, y))
if fails:
    print("\nFAILURES:")
    for f in fails: print(" -", f)
    sys.exit(1)
print("\nFRONT BEZEL COUNTERBORE / THROUGH-HOLE VALIDATION PASS")
