"""Export the three printable assembly parts to STL and re-import to verify meshes."""
import FreeCAD as App
import Mesh, os, sys
HERE=os.path.dirname(os.path.abspath(__file__))
OUT=os.path.join(HERE,"stl")
os.makedirs(OUT,exist_ok=True)
doc=App.openDocument(os.path.join(HERE,"EInk_Weather_Display_Assembly.FCStd"))
parts=[("PRINT_FRONT_BEZEL","front_bezel.stl"),
       ("PRINT_REAR_CHASSIS","rear_chassis.stl"),
       ("PRINT_REAR_COVER","rear_cover.stl"),
       ("PRINT_SERVICE_HATCH","service_hatch.stl"),
       ("PRINT_SWING_FOOT","swing_foot.stl")]
for name,fn in parts:
    o=doc.getObject(name)
    if not o or o.Shape.isNull() or len(o.Shape.Solids)!=1 or not o.Shape.isValid():
        raise RuntimeError(name+" is not one valid solid")
    path=os.path.join(OUT,fn)
    Mesh.export([o],path)
    m=Mesh.Mesh(path)
    if m.CountFacets<100 or m.Volume<=0:
        raise RuntimeError("bad mesh "+fn)
    print("%-22s facets %6d volume %10.1f bytes %d"%(fn,m.CountFacets,m.Volume,os.path.getsize(path)))
print("exported",OUT)
