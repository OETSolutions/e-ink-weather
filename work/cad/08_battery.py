"""User-specified Li-ion pouch model: 70 x 39 x 11 mm.
Reference component only, not printable. Placement is finalized beside the board after
P5/FPC fixes the board position.
"""
import FreeCAD as App
import Part
import os, math
HERE=os.path.dirname(os.path.abspath(__file__))
W,H,T=70.0,39.0,11.0                           # USER
R=4.0                                           # ASSUME typical pouch corner radius

P=lambda x,y:App.Vector(x,y,0)
wire=Part.Wire([
 Part.makeLine(P(R,0),P(W-R,0)),
 Part.ArcOfCircle(Part.Circle(P(W-R,R),App.Vector(0,0,1),R),math.radians(270),math.radians(360)).toShape(),
 Part.makeLine(P(W,R),P(W,H-R)),
 Part.ArcOfCircle(Part.Circle(P(W-R,H-R),App.Vector(0,0,1),R),math.radians(0),math.radians(90)).toShape(),
 Part.makeLine(P(W-R,H),P(R,H)),
 Part.ArcOfCircle(Part.Circle(P(R,H-R),App.Vector(0,0,1),R),math.radians(90),math.radians(180)).toShape(),
 Part.makeLine(P(0,H-R),P(0,R)),
 Part.ArcOfCircle(Part.Circle(P(R,R),App.Vector(0,0,1),R),math.radians(180),math.radians(270)).toShape(),
])
body=Part.Face(wire).extrude(App.Vector(0,0,T))
# Wire exit tab is explicitly an assumption; outside the user's specified envelope and used
# only to visualize routing. It is hidden by default in the assembly later.
tab=Part.makeBox(12,7,0.4,App.Vector(W/2-6,H,5.3))

doc=App.newDocument("Battery")
o=doc.addObject("Part::Feature","BATTERY_70x39x11"); o.Label="REFERENCE: Li-ion battery 70 x 39 x 11 (USER)"; o.Shape=body
o.addProperty("App::PropertyString","Source").Source="User-specified 70 x 39 x 11 mm"
o.addProperty("App::PropertyString","NotForPrinting").NotForPrinting="Reference component"
t=doc.addObject("Part::Feature","BATTERY_WIRE_TAB_ASSUMED"); t.Shape=tab
t.addProperty("App::PropertyString","Assumption").Assumption="12 x 7 x 0.4 wire-exit tab; replace after measuring physical cell"
doc.recompute(); doc.saveAs(os.path.join(HERE,"battery.FCStd"))
print("battery bbox %.1f x %.1f x %.1f valid %s"%(body.BoundBox.XLength,body.BoundBox.YLength,body.BoundBox.ZLength,body.isValid()))
print("saved battery.FCStd")
