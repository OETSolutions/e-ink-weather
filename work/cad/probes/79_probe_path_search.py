"""Does ANY assembly motion exist? Search rotation x mouth-axis translation.

The bore is concentric with the pin at every foot angle, so rotation about the pin axis can
never move the pin relative to the clip. Assembly must therefore be a TRANSLATION along the
clip's mouth axis. For each foot angle we find the mouth direction from the solid, then sweep
the foot in from far out along that axis and measure interference at every step.
"""
import os
import sys
import math

import FreeCAD as App
import Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib

m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.path.join(HERE, "case_shell.FCStd"))
outer = doc.getObject("CASE_SHELL").Shape
foot0 = m.build_foot()
m.STOP_ENABLE = False
cover = m.build_cover(outer, foot0)

HINGE = App.Vector(0, m.HINGE_Y, m.KNUCKLE_Z)
AXIS = App.Vector(1, 0, 0)
HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
CX = m.ARM_CENTERS[1]
BORE_R = m.BORE_R
PIN_R = m.PIN_R
OUT_R = BORE_R + m.CLIP_WALL


def rotated(deg):
    f = foot0.copy()
    if deg:
        f.rotate(HINGE, AXIS, -float(deg))
    return f


def mouth_dir(deg):
    """Centre angle of the open arc at radius BORE_R+0.35, measured from the rotated solid."""
    f = rotated(deg)
    rr = BORE_R + 0.35
    opens = []
    for i in range(360):
        a = math.radians(i)
        p = App.Vector(CX, HY + rr * math.sin(a), HZ + rr * math.cos(a))
        b = Part.makeBox(0.25, 0.25, 0.25, App.Vector(CX - 0.125, p.y - 0.125, p.z - 0.125))
        if f.common(b).Volume <= 0.006:
            opens.append(i)
    if not opens:
        return None
    # longest run (circular)
    flags = [i in set(opens) for i in range(360)]
    best = cur = 0
    start = 0
    for i in range(720):
        if flags[i % 360]:
            cur += 1
            if cur > best:
                best = cur
                start = (i - cur + 1) % 360
        else:
            cur = 0
    centre = (start + best / 2.0) % 360
    return centre, best


def offbearing(shape):
    inter = shape.common(cover)
    tot = inter.Volume
    for c in m.ARM_CENTERS:
        s = Part.makeBox(3.4, 300, 300, App.Vector(c - 1.7, -60, -100))
        tot -= inter.common(s).Volume
    return max(tot, 0.0)


print("phi  mouth(deg,arc)   dir(Y,Z)        clear-from(mm)   worst off-bearing on the way in")
results = []
for phi in range(0, 135, 5):
    md = mouth_dir(phi)
    if md is None:
        print(" %3d  no open arc" % phi)
        continue
    cdeg, arc = md
    a = math.radians(cdeg)
    dy, dz = math.sin(a), math.cos(a)
    worst = 0.0
    worst_s = None
    clear_from = None
    for si in range(0, 161):
        s = si * 0.05
        f = rotated(phi)
        f.translate(App.Vector(0, -s * dy, -s * dz))
        ob = offbearing(f)
        if ob > worst:
            worst = ob
            worst_s = s
        if ob > 0.05 and clear_from is None:
            clear_from = s
    results.append((phi, cdeg, arc, dy, dz, clear_from, worst, worst_s))
    print(" %3d  %5.0f (%3d)   (%6.3f,%6.3f)   %8s   %9.3f at s=%.2f"
          % (phi, cdeg, arc, dy, dz,
             "--" if clear_from is None else "%.2f" % clear_from, worst,
             -1 if worst_s is None else worst_s))

print("\n=== best candidates: smallest worst-case interference ===")
for r in sorted(results, key=lambda r: r[6])[:8]:
    print("  phi %3d  mouth %5.0f  worst off-bearing %8.3f at s=%.2f  clear_from %s"
          % (r[0], r[1], r[6], -1 if r[7] is None else r[7], r[5]))
