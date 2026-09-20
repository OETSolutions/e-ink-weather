"""Map the stop's real support and the space beneath it.

User: "For the stops, why don't you have the stop extending all the way down below, there's
nothing under it and it will still be very weak cantilevered out like that."

Show the cover's Y-Z material around the hinge so the cantilever is visible, and find the
assembly's lowest point to see whether a deeper stop would protrude.
"""
import os, sys, math
import FreeCAD as App, Part

HERE = "/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/work/cad"
sys.path.insert(0, HERE)
import importlib
m = importlib.import_module("06_rear_cover_foot")

d = App.openDocument(os.path.join(HERE, "rear_cover_foot.FCStd"))
cover = d.getObject("PRINT_REAR_COVER").Shape
bb = cover.BoundBox
print("cover bbox X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f"
      % (bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, bb.ZMax))

ys = range(0, 15)
zs = range(-10, 4)
print("\n=== cover (+stop) Y-Z occupancy ===")
for X in [30.0, 33.0, 36.0, 40.0, 41.5, 45.0, 50.0, 93.0, 100.0, 104.0]:
    print("\n--- X %.1f  (%s) ---" % (X, "SPAN" if not (40.6 < X < 93.8) else "GAP between spans"))
    print("       " + "".join("%4d" % z for z in zs))
    for y in ys:
        row = "  y %4.1f" % y
        for z in zs:
            row += "  %2s" % ("#" if cover.isInside(App.Vector(X, y, z), 1e-6, True) else ".")
        print(row)

print("\n=== where the stop sits in Z, per X (stop spans only) ===")
for X in [30.0, 34.0, 38.0, 40.0, 94.0, 100.0, 104.0]:
    col = cover.common(Part.makeBox(0.02, 40, 40, App.Vector(X, -5, -30)))
    cbb = col.BoundBox
    print("   X %5.1f : cover material Y %.2f..%.2f  Z %.2f..%.2f"
          % (X, cbb.YMin, cbb.YMax, cbb.ZMin, cbb.ZMax))

print("\n=== the assembly's lowest point ===")
a = App.openDocument(os.path.join(HERE, "EInk_Weather_Display_Assembly.FCStd"))
for o in a.Objects:
    try:
        s = o.Shape
        if s.Volume > 0:
            print("   %-30s Z %8.2f .. %6.2f" % (o.Name, s.BoundBox.ZMin, s.BoundBox.ZMax))
    except Exception:
        pass

print("\n=== STOP constant block, as built ===")
ca, sa = math.cos(math.radians(m.SWING_DEG)), math.sin(math.radians(m.SWING_DEG))
py, pz = m.STOP_RAMP_P
print("   bearing face  (%.2f,%.2f) -> (%.2f,%.2f)"
      % (py, pz, py + m.STOP_RAMP_LEN * ca, pz - m.STOP_RAMP_LEN * sa))
print("   ramp back     y %.2f..%.2f  z %.2f..%.2f"
      % (m.STOP_RAMP_P[0] - m.STOP_RAMP_T * sa, m.STOP_RAMP_P[0],
         m.STOP_RAMP_P[1] - m.STOP_RAMP_T * ca, m.STOP_RAMP_P[1]))
print("   spans  %s" % str(m.STOP_SPAN_X))
print("   risers %s   Y %.2f..%.2f  Z %.2f..%.2f"
      % (str(m.STOP_RISER_X), m.STOP_RISER_Y0, m.STOP_RISER_Y1,
         m.STOP_RISER_Z0, m.STOP_RISER_Z1))
