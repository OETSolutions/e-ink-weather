"""Why the service hatch looks odd: measure, don't guess. Also tests cleaner outlines.

Two things the renders show and the question is about:
  * a thin ring around the whole hatch where the anti-rotation LIP sticks out past the
    exterior FLANGE, so the hatch reads as stepped rather than flush;
  * a plain sharp-cornered rectangular tab welded onto one side to reach the screw.

Run:  freecadcmd 41_probe_hatch_outline.py
"""
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")


def build(flange, tab=None):
    m.HATCH_FLANGE_X0, m.HATCH_FLANGE_X1 = flange[0], flange[1]
    m.HATCH_FLANGE_Y0, m.HATCH_FLANGE_Y1 = flange[2], flange[3]
    if tab is not None:
        m.HATCH_TAB_X0, m.HATCH_TAB_X1, m.HATCH_TAB_Y0, m.HATCH_TAB_Y1 = tab
    return m.build_hatch()


def main():
    src = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
    outer = src.getObject("CASE_SHELL").Shape
    cover = m.build_cover(outer, None)

    lip_x0 = m.SERVICE_BAY_X0 - (m.HATCH_LIP_CLEAR + m.HATCH_LIP_T)
    lip_x1 = m.SERVICE_BAY_X1 + (m.HATCH_LIP_CLEAR + m.HATCH_LIP_T)
    lip_y0 = m.SERVICE_BAY_Y0 - (m.HATCH_LIP_CLEAR + m.HATCH_LIP_T)
    lip_y1 = m.SERVICE_BAY_Y1 + (m.HATCH_LIP_CLEAR + m.HATCH_LIP_T)

    print("bay            X %.2f..%.2f  Y %.2f..%.2f"
          % (m.SERVICE_BAY_X0, m.SERVICE_BAY_X1, m.SERVICE_BAY_Y0, m.SERVICE_BAY_Y1))
    print("lip (outer)    X %.2f..%.2f  Y %.2f..%.2f" % (lip_x0, lip_x1, lip_y0, lip_y1))
    print("flange (now)   X %.2f..%.2f  Y %.2f..%.2f"
          % (m.HATCH_FLANGE_X0, m.HATCH_FLANGE_X1,
             m.HATCH_FLANGE_Y0, m.HATCH_FLANGE_Y1))
    print()
    print("DEFECT 1: the lip overhangs the flange by"
          "  -X %.2f  +X %.2f  -Y %.2f  +Y %.2f mm"
          % (m.HATCH_FLANGE_X0 - lip_x0, lip_x1 - m.HATCH_FLANGE_X1,
             m.HATCH_FLANGE_Y0 - lip_y0, lip_y1 - m.HATCH_FLANGE_Y1))
    tb = (m.HATCH_TAB_X0, m.HATCH_TAB_X1, m.HATCH_TAB_Y0, m.HATCH_TAB_Y1)
    print("DEFECT 2: screw tab X %.2f..%.2f  Y %.2f..%.2f  = %.1f x %.1f mm,"
          " sticking %.2f mm past the flange"
          % (tb[0], tb[1], tb[2], tb[3], tb[1] - tb[0], tb[3] - tb[2],
             tb[1] - m.HATCH_FLANGE_X1))
    print("          screw axis (%.1f, %.1f) head dia %.1f; tab is %.1f x %.1f"
          % (m.HATCH_SCREW_X, m.HATCH_SCREW_Y, m.HATCH_SCREW_HEAD_D,
             tb[1] - tb[0], tb[3] - tb[2]))
    print()

    cands = [
        ("A current", (m.HATCH_FLANGE_X0, m.HATCH_FLANGE_X1,
                       m.HATCH_FLANGE_Y0, m.HATCH_FLANGE_Y1), tb),
        ("B lap 0.5", (lip_x0 - 0.5, lip_x1 + 0.5, lip_y0 - 0.5, lip_y1 + 0.5), tb),
        ("C lap 1.0", (lip_x0 - 1.0, lip_x1 + 1.0, lip_y0 - 1.0, lip_y1 + 1.0), tb),
        ("D one plate", (lip_x0 - 0.5, m.HATCH_TAB_X1, lip_y0 - 0.5, lip_y1 + 0.5), tb),
    ]
    print(" %-12s | flange rect                        | interfer vs cover | volume")
    for name, fl, tab in cands:
        try:
            h = build(fl, tab)
            v = h.common(cover).Volume
            print(" %-12s | X %6.2f..%6.2f Y %6.2f..%6.2f | %14.4f mm3 | %.0f"
                  % (name, fl[0], fl[1], fl[2], fl[3], v, h.Volume))
        except Exception as e:
            print(" %-12s | FAILED: %s" % (name, e))

    # Sharp edges: knife edges and slivers that print badly.
    print()
    for name, fl, tab in cands:
        h = build(fl, tab)
        short = [e.Length for e in h.Edges if 0 < e.Length < 0.8]
        print(" %-12s | edges < 0.8 mm: %d  %s"
              % (name, len(short), ["%.3f" % v for v in sorted(short)[:6]]))


main()
