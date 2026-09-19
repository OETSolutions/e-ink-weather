import FreeCAD as App, os, sys
HERE=os.path.dirname(os.path.abspath(__file__))
p=os.path.join(HERE,"EInk_Weather_Display_Assembly.FCStd")
d=App.openDocument(p)
print("document",d.Name,"objects",len(d.Objects))
for gname in ["PRINTABLE_CASE_PARTS","DISPLAY_ASSEMBLY","ESP32_M1_ASSEMBLY","POWER_ASSEMBLY","HARDWARE_REFERENCE"]:
 g=d.getObject(gname)
 if not g: raise RuntimeError("missing group "+gname)
 print("group %-24s members %d"%(gname,len(g.Group)))
prints=d.getObject("PRINTABLE_CASE_PARTS").Group
if len(prints)!=5: raise RuntimeError("expected exactly 5 printable parts")
for o in prints:
 print(" print %-24s solid %d valid %s vol %.1f"%(o.Name,len(o.Shape.Solids),o.Shape.isValid(),o.Shape.Volume))
# required real components
for n in ["PANEL_GLASS","PANEL_FPC_FOLDED_180","BOARD_PCB_48x66x1","BOARD_P5_ENTRY","BATTERY_70x39x11"]:
 o=d.getObject(n)
 if not o: raise RuntimeError("missing "+n)
 print(" required",n,"OK")
print("ASSEMBLY STRUCTURE PASS")
