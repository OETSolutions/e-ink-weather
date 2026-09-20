"""The hatch: can it actually be inserted?

The user: "The rear cover door has a groove along its edges that I guess the rear cover edges
are supposed to fit into. But because it's a groove and not open on the inside, you can't put
the door in (connector hatch, as it's called)!!! The inside portion of the hatch needs to be
able to fit through the hole."

So: the hatch's PLUG + LIP must pass through the BAY OPENING. Measure whether they can.
The plug is bay - 2*HATCH_CLEAR. The lip is LARGER than the bay (it is an anti-rotation lip that
drops into a recess in the cover). If the lip is bigger than the opening, the hatch cannot go in.
"""
import os
import sys
import math

import FreeCAD as App
import Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = doc.getObject("CASE_SHELL").Shape
foot = m.build_foot()
cover = m.build_cover(outer, foot)
hatch = m.build_hatch()

print("BAY OPENING   X %.2f..%.2f  Y %.2f..%.2f"
      % (m.SERVICE_BAY_X0, m.SERVICE_BAY_X1, m.SERVICE_BAY_Y0, m.SERVICE_BAY_Y1))
print("  size %.2f x %.2f" % (m.SERVICE_BAY_X1 - m.SERVICE_BAY_X0,
                              m.SERVICE_BAY_Y1 - m.SERVICE_BAY_Y0))
print("HATCH LIP     X %.2f..%.2f  Y %.2f..%.2f"
      % (m.HATCH_LIP_OUT[0], m.HATCH_LIP_OUT[2], m.HATCH_LIP_OUT[1], m.HATCH_LIP_OUT[3]))
print("  size %.2f x %.2f" % (m.HATCH_LIP_OUT[2] - m.HATCH_LIP_OUT[0],
                              m.HATCH_LIP_OUT[3] - m.HATCH_LIP_OUT[1]))
print("PLUG (bay - 2*clear = -2*%.2f):  %.2f x %.2f"
      % (m.HATCH_CLEAR,
         (m.SERVICE_BAY_X1 - m.SERVICE_BAY_X0) - 2 * m.HATCH_CLEAR,
         (m.SERVICE_BAY_Y1 - m.SERVICE_BAY_Y0) - 2 * m.HATCH_CLEAR))
print("\nLIP is %.2f mm LARGER than the opening in X and %.2f in Y -> it cannot pass through"
      % ((m.HATCH_LIP_OUT[2] - m.HATCH_LIP_OUT[0]) - (m.SERVICE_BAY_X1 - m.SERVICE_BAY_X0),
         (m.HATCH_LIP_OUT[3] - m.HATCH_LIP_OUT[1]) - (m.SERVICE_BAY_Y1 - m.SERVICE_BAY_Y0)))

print("\n=== what does the COVER look like around the bay? (is the recess a closed groove?) ===")
# Slice the cover across the bay edge, in Z, along a line through the middle of the bay's X.
ymid = (m.SERVICE_BAY_Y0 + m.SERVICE_BAY_Y1) / 2.0
print("   slicing the cover at X %.2f, Y from bay-4 to bay+2" % ((m.SERVICE_BAY_X0 + m.SERVICE_BAY_X1) / 2))
print("   Y      cover Z-runs")
xmid = (m.SERVICE_BAY_X0 + m.SERVICE_BAY_X1) / 2.0
for yi in range(int(m.SERVICE_BAY_Y0) - 5, int(m.SERVICE_BAY_Y1) + 2):
    y = float(yi)
    runs = []
    inside = False
    s = None
    for zi in range(-40, 60):
        z = zi * 0.05
        v = cover.isInside(App.Vector(xmid, y, z), 1e-6, True)
        if v and not inside:
            s = z
            inside = True
        elif not v and inside:
            runs.append((s, z))
            inside = False
    if inside:
        runs.append((s, 3.0))
    print("   %5.1f  %s" % (y, runs))

print("\n=== HATCH solid: Z extent of plug and lip ===")
for s in hatch.Solids:
    bb = s.BoundBox
    print("   solid %.2f mm3  X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
          % (s.Volume, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

print("\n=== can the hatch be pushed in along -Z? measure the overlap with the cover ===")
for dz in [0, 1, 2, 3, 4, 5]:
    h = hatch.copy()
    h.translate(App.Vector(0, 0, dz))
    print("   dz %+d : hatch x cover overlap %.4f mm3" % (dz, h.common(cover).Volume))
