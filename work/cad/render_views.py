"""Wireframe orthographic projections of a shape, written as SVG (no external deps)."""
import FreeCAD as App
import sys, os, math

src = os.environ["SRC"]
objname = os.environ.get("OBJ") or None
outbase = os.environ.get("OUT") or os.path.splitext(src)[0]

doc = App.openDocument(src)
objs = [doc.getObject(objname)] if objname else [o for o in doc.Objects if hasattr(o, "Shape") and o.Shape.Edges]
print("objects:", [o.Name for o in objs])


def sample(shape, n=48):
    """Return list of (point, edge_key) polylines."""
    polys = []
    for e in shape.Edges:
        try:
            a, b = e.FirstParameter, e.LastParameter
            pts = [e.valueAt(a + (b - a) * i / float(n)) for i in range(n + 1)]
            polys.append(pts)
        except Exception:
            pass
    return polys


def project(pts, mode):
    out = []
    for p in pts:
        if mode == "front":
            out.append((p.x, -p.y))
        elif mode == "side":
            out.append((p.y, -p.z))
        elif mode == "top":
            out.append((p.x, p.z))
        elif mode == "iso":
            v = App.Rotation(App.Vector(1, 0, 0), -60).multVec(App.Rotation(App.Vector(0, 0, 1), 30).multVec(p))
            out.append((v.x - 0.5 * v.y, -v.z + 0.35 * v.y))
        elif mode == "iso2":
            v = App.Rotation(App.Vector(1, 0, 0), -70).multVec(App.Rotation(App.Vector(0, 0, 1), -135).multVec(p))
            out.append((v.x - 0.5 * v.y, -v.z + 0.35 * v.y))
    return out


def write_svg(polys, path, w=1200, pad=20, colour="#111"):
    xs = [q[0] for pl in polys for q in pl]
    ys = [q[1] for pl in polys for q in pl]
    if not xs:
        return
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    sx = (w - 2 * pad) / max(x1 - x0, 1e-9)
    h = int((y1 - y0) * sx + 2 * pad)
    sy = sx
    with open(path, "w") as f:
        f.write('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">' % (w, h, w, h))
        f.write('<rect width="100%%" height="100%%" fill="white"/>')
        f.write('<g stroke="%s" stroke-width="1" fill="none">' % colour)
        for pl in polys:
            d = " ".join("%.2f,%.2f" % (pad + (q[0] - x0) * sx, pad + (q[1] - y0) * sy) for q in pl)
            f.write('<polyline points="%s"/>' % d)
        f.write("</g></svg>")


for mode in ["front", "side", "top", "iso", "iso2"]:
    polys = []
    for o in objs:
        for pl in sample(o.Shape, 40):
            polys.append(project(pl, mode))
    p = "%s_%s.svg" % (outbase, mode)
    write_svg(polys, p)
    print("wrote", p)
