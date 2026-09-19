"""Design constraints for the redesign: what cover material can a stop be anchored to, and
what does the real assembly motion require?

Findings so far:
  * the stop ramp welds to the cover over only 0.707 mm3 (a ~0.17 x 0.17 mm sliver) -- the
    user is right that it will break off.
  * the cover has NO material below Z -2.88 except the lower wall at Y 1.0..7.2.
  * the pin spans X 28.2..106.2; the plate spans X 32.2..102.2; clips at 44.2/67.2/90.2.

This maps exactly where a stop can be anchored with a real weld, and measures the assembly
motion the clips actually need.
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
foot = m.build_foot()
m.STOP_ENABLE = False
cover = m.build_cover(outer, foot)
m.STOP_ENABLE = True

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
HINGE = App.Vector(0, HY, HZ)
AXIS = App.Vector(1, 0, 0)

print("=== where is there cover material to anchor a stop, BELOW the plate's swing? ===")
print("The plate's underside at 65 deg runs from (7.10,-3.56) at 65 deg. Material the stop")
print("can grow from must lie below/behind that line.\n")
# Map cover occupancy on a Y-Z grid in the region below the hinge.
print("cover material map (Y across, Z down). '#' = material present")
ys = [2.0 + 0.5 * i for i in range(24)]      # 2 .. 13.5
print("        " + "".join("%4.1f" % y for y in ys))
for zi in range(20, -130, -5):
    z = zi / 10.0
    row = ""
    for y in ys:
        b = Part.makeBox(0.4, 0.4, 0.4, App.Vector(60.0, y - 0.2, z - 0.2))
        row += "%4s" % ("#" if cover.common(b).Volume > 0.004 else ".")
    print("Z %6.1f %s" % (z, row))

print("\n=== clip mouth direction, measured ===")
CX = m.ARM_CENTERS[1]
sample_r = m.BORE_R + 0.27
open_deg = []
for i in range(360):
    a = math.radians(i)
    y = HY + sample_r * math.sin(a)
    z = HZ + sample_r * math.cos(a)
    b = Part.makeBox(0.2, 0.2, 0.2, App.Vector(CX - 0.1, y - 0.1, z - 0.1))
    if foot.common(b).Volume <= 0.005:
        open_deg.append(i)
# find the largest continuous run
runs, cur = [], []
for i in range(720):
    d = open_deg[i % 360] if i < 360 else open_deg[i - 360]
    if i < 360 and d == i:
        cur.append(d)
    elif i >= 360:
        break
# simpler: report the gaps
gaps = sorted(set(range(360)) - set(open_deg))
print("open (mouth) pin angles: %s" % (open_deg if len(open_deg) < 40 else
      "%d..%d and %d..%d (%d total)" % (open_deg[0], open_deg[len(open_deg)//2],
                                        open_deg[len(open_deg)//2+1], open_deg[-1], len(open_deg))))
mid = open_deg[len(open_deg) // 2] if open_deg else None
print("mouth centre pin angle ~%s deg (0=+Z up, 90=+Y, 180=-Z, 270=-Y)" % mid)

print("\n=== assembly: what motion brings the pin into the mouths? ===")
print("clip mouth is centred on -Y (the folded side). To seat, the pin must travel +Y")
print("relative to the clip, OR the clip must be rotated about the pin.")
for dy in (0.0, -2.0, -4.0, -6.0, -8.0, -10.0):
    g = foot.copy()
    if dy:
        g.translate(App.Vector(0, dy, 0))
    print("   clip moved %5.1f mm in -Y : clash %8.3f mm3" % (dy, g.common(cover).Volume))
