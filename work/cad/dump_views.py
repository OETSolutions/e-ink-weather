"""Sample a shape's edges, project to several fixed views, and dump polylines as JSON.
Run under freecadcmd with env: SRC, OBJ (optional), OUT (json path)."""
import FreeCAD as App
import os, json

src = os.environ["SRC"]
objname = os.environ.get("OBJ") or None
out = os.environ["OUT"]

doc = App.openDocument(src)
if objname:
    objs = [doc.getObject(objname)]
else:
    objs = [o for o in doc.Objects if hasattr(o, "Shape") and o.Shape.Edges]
print("objects:", [o.Name for o in objs])


def sample(shape, n=64):
    for e in shape.Edges:
        try:
            a, b = e.FirstParameter, e.LastParameter
            yield [e.valueAt(a + (b - a) * i / float(n)) for i in range(n + 1)]
        except Exception:
            pass


def proj(p, mode):
    if mode == "front":
        return (p.x, -p.y)
    if mode == "side":
        return (p.y, -p.z)
    if mode == "top":
        return (p.x, p.z)
    if mode == "iso":
        v = App.Rotation(App.Vector(1, 0, 0), -62).multVec(App.Rotation(App.Vector(0, 0, 1), 35).multVec(p))
        return (v.x - 0.55 * v.y, -v.z + 0.30 * v.y)
    if mode == "iso2":
        v = App.Rotation(App.Vector(1, 0, 0), -74).multVec(App.Rotation(App.Vector(0, 0, 1), -140).multVec(p))
        return (v.x - 0.55 * v.y, -v.z + 0.30 * v.y)


views = {}
for mode in ["front", "side", "top", "iso", "iso2"]:
    poly = []
    for o in objs:
        for pl in sample(o.Shape):
            poly.append([[round(q, 4) for q in proj(pt, mode)] for pt in pl])
    views[mode] = poly
    print("view %-6s %d polylines" % (mode, len(poly)))

json.dump(views, open(out, "w"))
print("wrote", out)
