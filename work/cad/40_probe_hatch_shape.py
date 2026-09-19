"""Describe the service hatch's actual shape so the "weird artifacts" can be named.

Prints the outline polygon at each z level that matters, every planar face with its normal and
area, and the tab's real extents.

Run:  freecadcmd 40_probe_hatch_shape.py
"""
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")


def outline_at(shape, z):
    """Bounding outline of the shape's cross-section at height z, as min/max rectangles."""
    plane = Part.makeBox(400, 400, 0.002, App.Vector(-100, -100, z))
    sec = shape.common(plane)
    if sec.Volume <= 0 and not sec.Faces:
        return None
    bb = sec.optimalBoundingBox()
    return bb, sec.Volume


def main():
    doc = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
    h = doc.getObject("PRINT_SERVICE_HATCH").Shape
    print("hatch vol %.1f  solids %d  valid %s" % (h.Volume, len(h.Solids), h.isValid()))
    bb = h.optimalBoundingBox()
    print("bbox X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f"
          % (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))
    print()
    print("nominal: flange X %.2f..%.2f  Y %.2f..%.2f  T %.2f"
          % (m.HATCH_FLANGE_X0, m.HATCH_FLANGE_X1, m.HATCH_FLANGE_Y0, m.HATCH_FLANGE_Y1,
             m.HATCH_FLANGE_T))
    print("         bay    X %.2f..%.2f  Y %.2f..%.2f"
          % (m.SERVICE_BAY_X0, m.SERVICE_BAY_X1, m.SERVICE_BAY_Y0, m.SERVICE_BAY_Y1))
    print("         tab    X %.2f..%.2f  Y %.2f..%.2f  T %.2f"
          % (m.HATCH_TAB_X0, m.HATCH_TAB_X1, m.HATCH_TAB_Y0, m.HATCH_TAB_Y1,
             m.HATCH_FLANGE_T))
    print("         USB slot W %.2f H %.2f at X %.2f"
          % (m.USB_SLOT_W, m.USB_SLOT_H, m.USB_SLOT_X))
    print()

    # Z-level cross sections through the part.
    for i in range(9):
        z = bb.ZMin + (bb.ZMax - bb.ZMin) * i / 8.0
        strip = Part.makeBox(400, 400, 0.02, App.Vector(-100, -100, z - 0.01))
        sec = h.common(strip)
        if sec.Volume <= 0:
            print("z %6.2f  (empty)" % z)
            continue
        sbb = sec.optimalBoundingBox()
        n = len(sec.Faces)
        print("z %6.2f  X %7.2f..%7.2f  Y %7.2f..%7.2f  area ~%7.1f  faces %d"
              % (z, sbb.XMin, sbb.XMax, sbb.YMin, sbb.YMax, sec.Volume / 0.02, n))

    print()
    print("perimeter at the flange's mid-depth (the visible exterior outline):")
    z = -m.HATCH_FLANGE_T / 2.0
    strip = Part.makeBox(400, 400, 0.02, App.Vector(-100, -100, z - 0.01))
    sec = h.common(strip)
    wires = []
    for f in sec.Faces:
        for w in f.Wires:
            pts = [(round(v.Point.x, 2), round(v.Point.y, 2)) for v in w.Vertexes]
            wires.append(sorted(set(pts)))
    for w in wires:
        print("   wire with %d distinct vertices: %s" % (len(w), w[:14]))

    print()
    print("cylindrical faces (the screw bore / counterbore):")
    for i, f in enumerate(h.Faces):
        if f.Surface.TypeId == "Part::GeomCylinder":
            ax = f.Surface.Axis
            c = f.Surface.Center
            r = f.Surface.Radius
            fbb = f.BoundBox
            print("   face %d  r %.3f  axis %s  centre (%.2f,%.2f,%.2f)  Z %.2f..%.2f"
                  % (i, r, str(ax), c.x, c.y, c.z, fbb.ZMin, fbb.ZMax))

    print()
    print("sharp edges shorter than 0.8 mm (print artifacts / knife edges):")
    n = 0
    for e in h.Edges:
        L = e.Length
        if 0 < L < 0.8:
            bb2 = e.BoundBox
            print("   len %.3f  at X %.2f..%.2f Y %.2f..%.2f Z %.2f..%.2f"
                  % (L, bb2.XMin, bb2.XMax, bb2.YMin, bb2.YMax, bb2.ZMin, bb2.ZMax))
            n += 1
    print("   total %d" % n)


main()
