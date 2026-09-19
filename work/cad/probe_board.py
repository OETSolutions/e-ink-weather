"""Import the project's ESP32-M1 STEP and report its geometry, looking for P5."""
import FreeCAD as App
import Part
import os

HERE = os.path.dirname(os.path.abspath(__file__))
STEP = os.path.join(HERE, "..", "..", "GoodDisplay_ESP32_M1_reconstructed_v2",
                    "GoodDisplay_ESP32_M1_reconstructed.step")
STEP = os.path.abspath(STEP)
print("STEP:", STEP, "exists:", os.path.exists(STEP))

doc = App.newDocument("BoardProbe")
Part.insert(STEP, doc.Name)
doc.recompute()

objs = [o for o in doc.Objects if hasattr(o, "Shape") and o.Shape.Solids]
print("objects with solids:", len(objs))
for o in objs:
    bb = o.Shape.BoundBox
    print("  %-28s bbox X %7.2f..%7.2f  Y %7.2f..%7.2f  Z %7.2f..%7.2f  vol %8.1f" %
          (o.Name, bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax, o.Shape.Volume))

whole = Part.makeCompound([o.Shape for o in objs]) if objs else None
bb = whole.BoundBox
print("\nCOMPOUND bbox X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f  solids %d" %
      (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax, len(whole.Solids)))
print("size: %.2f x %.2f x %.2f" % (bb.XLength, bb.YLength, bb.ZLength))
