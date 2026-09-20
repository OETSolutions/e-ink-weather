"""Push a USB-C plug through the service hatch's cable slot and find where it jams.

This is the test that answers "the cable can't be put there". It steps a plug-sized solid
in from outside the case (+Y) to the board's USB port (-Y), reporting the first step at
which it collides with the cover or the hatch.

Env: SRC
"""
import importlib
import os
import sys

import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
m = importlib.import_module("06_rear_cover_foot")

doc = App.openDocument(os.environ.get("SRC", "rear_cover_foot.FCStd"))
cover = doc.getObject("PRINT_REAR_COVER").Shape
hatch = doc.getObject("PRINT_SERVICE_HATCH").Shape

# A USB-C plug body: 8.4 x 2.6 mm mating face, plus a 6 mm overmould behind it.
# The slot is USB_SLOT_W x USB_SLOT_H. Model the plug at 0.4 mm under each so the test is
# about the SLOT, not about a nominal-vs-nominal clash.
PLUG_W = m.USB_SLOT_W - 0.4
PLUG_H = m.USB_SLOT_H - 0.4
PLUG_LEN = 6.0

print("slot: X %.2f..%.2f  Y %.2f..%.2f  Z %.2f..%.2f"
      % (m.USB_SLOT_X - m.USB_SLOT_W / 2, m.USB_SLOT_X + m.USB_SLOT_W / 2,
         m.SERVICE_BAY_Y1 - 1.0, m.SERVICE_BAY_Y1 - 1.0 + 6.0, -0.2, -0.2 + m.USB_SLOT_H))
print("plug: %.2f wide x %.2f tall, %g long" % (PLUG_W, PLUG_H, PLUG_LEN))

# Plug travels in -Y. It is centred on the slot in X and Z.
cz = (-0.2 + (-0.2 + m.USB_SLOT_H)) / 2.0
print("plug centre: X %.2f  Z %.2f" % (m.USB_SLOT_X, cz))
print()
print("  Y_in  |  cover overlap mm3  |  hatch overlap mm3")
yj = m.SERVICE_BAY_Y1 + 12.0
step = 0.5
first_block = None
while yj >= m.SERVICE_BAY_Y1 - 12.0:
    plug = Part.makeBox(PLUG_W, PLUG_LEN, PLUG_H,
                        App.Vector(m.USB_SLOT_X - PLUG_W / 2, yj - PLUG_LEN / 2,
                                   cz - PLUG_H / 2))
    oc = cover.common(plug).Volume
    oh = hatch.common(plug).Volume
    flag = ""
    if (oc > 1e-6 or oh > 1e-6) and first_block is None:
        first_block = (yj, oc, oh)
        flag = "   <-- FIRST BLOCK"
    print("  %6.2f |  %14.4f     |  %14.4f%s" % (yj, oc, oh, flag))
    yj -= step

print()
if first_block:
    print("The plug FIRST hits something when its centre is at Y=%.2f (overlap cover %.4f, "
          "hatch %.4f mm3)." % first_block)
else:
    print("The plug passes clean through at every step: the slot is OPEN.")
