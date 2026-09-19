"""Where is the detent rib, and where is the groove? Verify they coincide when folded."""
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

HY, HZ = m.HINGE_Y, m.KNUCKLE_Z
PIN_R = m.PIN_R
print("MOUTH_PIN_ANGLE = %.0f" % m.MOUTH_PIN_ANGLE)
print("rib should be at pin angle MOUTH+180 = %.0f" % (m.MOUTH_PIN_ANGLE + 180.0))
print("DETENT_OPEN_PIN_ANGLE = %.0f" % m.DETENT_OPEN_PIN_ANGLE)
print("cover grooves cut at: %.0f and %.0f"
      % (m.MOUTH_PIN_ANGLE + 180.0, m.DETENT_OPEN_PIN_ANGLE))

# Where is the foot's rib, in pin-angle terms? Sample the foot at r = BORE_R, folded.
CX = m.ARM_CENTERS[1]
rr = m.BORE_R - m.DETENT_H / 2.0
print("\nfoot material at r %.2f around the axis, folded (pin angle: 0=+Z, 90=+Y, 180=-Z):"
      % rr)
hits = []
for i in range(360):
    a = math.radians(i)
    p = App.Vector(CX, HY + rr * math.sin(a), HZ + rr * math.cos(a))
    b = Part.makeBox(0.3, 0.3, 0.3, App.Vector(CX - 0.15, p.y - 0.15, p.z - 0.15))
    if foot.common(b).Volume > 0.01:
        hits.append(i)
runs = []
if hits:
    s = hits[0]
    prev = hits[0]
    for h in hits[1:]:
        if h != prev + 1:
            runs.append((s, prev))
            s = h
        prev = h
    runs.append((s, prev))
print("   foot solid angular runs:", runs)

# Where is the pin material removed (the groove)? Check the cover at r = PIN_R.
rr2 = PIN_R - m.DETENT_H / 2.0
print("\ncover material at r %.2f (inside the pin), folded:" % rr2)
hits2 = []
for i in range(360):
    a = math.radians(i)
    p = App.Vector(CX, HY + rr2 * math.sin(a), HZ + rr2 * math.cos(a))
    b = Part.makeBox(0.3, 0.3, 0.3, App.Vector(CX - 0.15, p.y - 0.15, p.z - 0.15))
    if cover.common(b).Volume > 0.01:
        hits2.append(i)
runs2 = []
if hits2:
    s = hits2[0]
    prev = hits2[0]
    for h in hits2[1:]:
        if h != prev + 1:
            runs2.append((s, prev))
            s = h
        prev = h
    runs2.append((s, prev))
print("   cover solid angular runs:", runs2)

# Directly: does the rib land in a groove?
rib_a = m.MOUTH_PIN_ANGLE + 180.0
groove_a = m.MOUTH_PIN_ANGLE + 180.0
print("\nrib at %.0f, groove at %.0f -> %s"
      % (rib_a, groove_a, "MATCH" if abs(rib_a - groove_a) < 1 else "MISMATCH"))
