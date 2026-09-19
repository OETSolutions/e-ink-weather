import FreeCAD as App, Part, os
p=os.path.join(os.path.dirname(os.path.abspath(__file__)),"EInk_Weather_Display_Assembly.FCStd")
d=App.openDocument(p)
f=d.getObject("PANEL_FPC_FOLDED_180").Shape
print("FPC bbox",f.BoundBox)
for n in ["BOARD_MICRO_SD_ENTRY","BOARD_L1_470","BOARD_P5_EPAPER_24PIN_FPC","BOARD_P5_ENTRY","BOARD_PCB_48x66x1"]:
 o=d.getObject(n); s=o.Shape; v=f.common(s).Volume
 print(n,"bbox",s.BoundBox,"vol",s.Volume,"inter",v,"dist",f.distToShape(s)[0])
# all individual intersections, no compound
for o in d.getObject("ESP32_M1_ASSEMBLY").Group:
 v=f.common(o.Shape).Volume
 dist=f.distToShape(o.Shape)[0]
 if v>1e-5 or dist<0.01:
  print("NEAR",o.Name,"bbox",o.Shape.BoundBox,"inter",v,"dist",dist)
