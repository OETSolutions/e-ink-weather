import FreeCAD as App
import Part
import math, os

OUT = os.path.dirname(os.path.abspath(__file__))
doc = App.openDocument(os.path.join(OUT, "case_shell.FCStd"))
body = doc.getObject("CASE_SHELL").Shape

D = 16.9
FW, FH = 133.4, 107.5
FR, RR = 3.4, 1.2


def expect(z):
    if z <= RR:
        f = RR - math.sqrt(max(RR ** 2 - z ** 2, 0))
    elif z >= D - FR:
        t = z - (D - FR)
        f = FR - math.sqrt(max(FR ** 2 - t ** 2, 0))
    else:
        f = 0.0
    return FW - 2 * f


print("   z     section_w   expect_w    diff")
for z in [0.05, 0.3, 0.6, 0.9, 1.2, 4.0, 8.0, 12.0, 13.5, 14.0, 14.5,
          15.0, 15.5, 16.0, 16.5, 16.85]:
    s = body.slice(App.Vector(0, 0, 1), z)
    if not isinstance(s, (list, tuple)):
        s = [s]
    xs = []
    for w_ in s:
        for e in w_.Edges:
            for v in e.Vertexes:
                xs.append(v.Point.x)
            for i in range(21):
                xs.append(e.valueAt(e.FirstParameter + (e.LastParameter - e.FirstParameter) * i / 20.0).x)
    if not xs:
        print("%6.2f  %9s  %9.3f  %s" % (z, "--", expect(z), "--"))
        continue
    w = max(xs) - min(xs)
    e_ = expect(z)
    print("%6.2f  %9.3f  %9.3f  %+7.3f" % (z, w, e_, w - e_))
