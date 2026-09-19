"""Which SIGN of rotation about the hinge axis is the real 'open' direction?

The reference foot placed in the document (REFERENCE_FOOT_OPEN_65DEG) is the ground truth.
Compare it against build_foot() rotated +65 and -65 and see which one matches. Everything
downstream (stop contact, open detent, interference) depends on getting this right.
"""
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__)) or "."
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
names = [o.Name for o in doc.Objects if o.TypeId.startswith("Part::")]
print("Part objects in doc:")
for o in doc.Objects:
    if "FOOT" in o.Name.upper() or "REFERENCE" in o.Name.upper():
        print("   %-40s %s" % (o.Name, o.TypeId))
        try:
            print("        Placement %s" % (o.Placement,))
        except Exception as e:
            print("        (no placement: %s)" % e)

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
foot = m.build_foot()
print("\nbuild_foot bbox: y %.2f..%.2f z %.2f..%.2f" %
      (foot.BoundBox.YMin, foot.BoundBox.YMax, foot.BoundBox.ZMin, foot.BoundBox.ZMax))

for sign, label in ((+1, "+65"), (-1, "-65")):
    f = foot.copy()
    f.rotate(HINGE, AXIS, sign * 65.0)
    bb = f.BoundBox
    print("rotated %s : y %.2f..%.2f  z %.2f..%.2f" % (label, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

# Compare to the reference, whatever its placement is.
ref = None
for o in doc.Objects:
    if "REFERENCE_FOOT" in o.Name.upper() and o.TypeId.startswith("Part::"):
        ref = o.Shape
        break
if ref is not None:
    bb = ref.BoundBox
    print("reference  : y %.2f..%.2f  z %.2f..%.2f  vol %.1f" %
          (bb.YMin, bb.YMax, bb.ZMin, bb.ZMax, ref.Volume))
    for sign, label in ((+1, "+65"), (-1, "-65")):
        f = foot.copy()
        f.rotate(HINGE, AXIS, sign * 65.0)
        # symmetric difference is the honest comparison
        diff = f.cut(ref).Volume + ref.cut(f).Volume
        print("   %s vs reference: symmetric-difference volume %.2f mm3" % (label, diff))
else:
    print("no reference foot found in the document")
