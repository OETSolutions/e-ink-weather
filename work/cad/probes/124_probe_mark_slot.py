"""Render the rear cover + hatch with the USB slot's footprint marked, so its location and
whether it is a hole are both unambiguous.

Adds a bright thin plate exactly filling the USB slot's rectangle (and one for the bay), then
renders the rear exterior. Violet = slot footprint, green/orange = cover, pink = hatch.

Env: (none)
"""
import importlib
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__))
CAD = os.path.dirname(HERE)
sys.path.insert(0, CAD)
m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.path.join(CAD, "rear_cover_foot.FCStd"))

sx0 = m.USB_SLOT_X - m.USB_SLOT_W / 2
sy0 = m.SERVICE_BAY_Y1 - 1.0
slot = Part.makeBox(m.USB_SLOT_W, 6.0, 0.3, App.Vector(sx0, sy0, 0.5))
o = doc.addObject("Part::Feature", "MARK_SLOT")
o.Shape = slot

# Bay outline as a wire frame box for reference.
bay = Part.makeBox(m.SERVICE_BAY_X1 - m.SERVICE_BAY_X0, m.SERVICE_BAY_Y1 - m.SERVICE_BAY_Y0,
                   0.3, App.Vector(m.SERVICE_BAY_X0, m.SERVICE_BAY_Y0, -1.5))
o2 = doc.addObject("Part::Feature", "MARK_BAY")
o2.Shape = bay

# Also mark the board's USB-C port projected onto the same plane, to show alignment.
asm = App.openDocument(os.path.join(CAD, "EInk_Weather_Display_Assembly.FCStd"))
pb = asm.getObject("BOARD_P2_USB_C_Port").Shape.BoundBox
port = Part.makeBox(pb.XLength, pb.YLength, 0.3,
                    App.Vector(pb.XMin, pb.YMin, -3.0))
o3 = doc.addObject("Part::Feature", "MARK_PORT")
o3.Shape = port
print("slot  x %.2f..%.2f y %.2f..%.2f" % (sx0, sx0 + m.USB_SLOT_W, sy0, sy0 + 6.0))
print("port  x %.2f..%.2f y %.2f..%.2f" % (pb.XMin, pb.XMax, pb.YMin, pb.YMax))

doc.recompute()
doc.saveAs("/tmp/marked.FCStd")
print("saved /tmp/marked.FCStd")
