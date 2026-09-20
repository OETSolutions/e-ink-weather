"""Which way is OUT? Material walks along each axis through the bay and the USB slot.

The exterior face of the cover and the direction a cable actually enters are not obvious
from the bounding box, and getting it wrong makes every clearance comment meaningless.
This walks lines through the real solids and prints the runs of cover / hatch material.

Env: SRC
"""
import os

import FreeCAD as App

doc = App.openDocument(os.environ.get("SRC", "rear_cover_foot.FCStd"))
cover = doc.getObject("PRINT_REAR_COVER").Shape
hatch = doc.getObject("PRINT_SERVICE_HATCH").Shape
foot = doc.getObject("PRINT_FOOT").Shape

print("cover bbox", cover.BoundBox)
print("hatch bbox", hatch.BoundBox)
print("foot  bbox", foot.BoundBox)


def runs(shape, a, b, n, axis, fixed):
    out = []
    prev = None
    start = None
    for i in range(n + 1):
        t = a + (b - a) * i / n
        if axis == "Y":
            p = App.Vector(fixed[0], t, fixed[1])
        elif axis == "Z":
            p = App.Vector(fixed[0], fixed[1], t)
        else:
            p = App.Vector(t, fixed[0], fixed[1])
        m = shape.isInside(p, 1e-7, True)
        if m and prev is not True:
            start = t
        if (not m) and prev is True:
            out.append((start, t))
        prev = m
    if prev:
        out.append((start, b))
    return out


def show(label, shape, a, b, n, axis, fixed):
    r = runs(shape, a, b, n, axis, fixed)
    print("  %-34s %s" % (label, "  ".join("[%.2f..%.2f]" % t for t in r) or "(none)"))


print("\n-- walk Z at the USB slot centre (x=29.60, y=94.0) --")
show("cover material in z", cover, -8.0, 4.0, 240, "Z", (29.60, 94.0))
show("hatch material in z", hatch, -8.0, 4.0, 240, "Z", (29.60, 94.0))

print("\n-- walk Z at the bay centre (x=50.85, y=80.0) --")
show("cover material in z", cover, -8.0, 4.0, 240, "Z", (50.85, 80.0))
show("hatch material in z", hatch, -8.0, 4.0, 240, "Z", (50.85, 80.0))

print("\n-- walk Y through the bay at (x=50.85) for several z --")
for z in (-1.0, 0.5, 1.5, 2.5):
    show("cover material in y (z=%.1f)" % z, cover, 60.0, 100.0, 200, "Y", (50.85, z))
    show("hatch material in y (z=%.1f)" % z, hatch, 60.0, 100.0, 200, "Y", (50.85, z))

print("\n-- walk Y at the USB slot (x=29.60) for several z --")
for z in (-1.5, -0.5, 0.5, 1.5, 2.5, 3.5):
    show("cover material in y (z=%.1f)" % z, cover, 88.0, 101.0, 130, "Y", (29.60, z))
    show("hatch material in y (z=%.1f)" % z, hatch, 88.0, 101.0, 130, "Y", (29.60, z))

print("\n-- walk X at the USB slot (y=94.0, z=1.5) --")
show("cover material in x", cover, 20.0, 40.0, 200, "X", (94.0, 1.5))
show("hatch material in x", hatch, 20.0, 40.0, 200, "X", (94.0, 1.5))

print("\n-- walk X through the key 1 notch (y=80.0, z=1.5) --")
show("cover material in x", cover, 14.0, 30.0, 160, "X", (80.0, 1.5))
show("hatch material in x", hatch, 14.0, 30.0, 160, "X", (80.0, 1.5))

print("\n-- walk Z through the key 1 tab (x=21.0, y=80.0) --")
show("cover material in z", cover, -6.0, 4.0, 200, "Z", (21.0, 80.0))
show("hatch material in z", hatch, -6.0, 4.0, 200, "Z", (21.0, 80.0))
