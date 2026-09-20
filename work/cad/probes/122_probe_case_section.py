"""YZ section through x=29.6 with EVERY case part, from the bezel to the cover.

Shows whether a cable entering the USB slot can reach the board's USB-C port, and what
stands between them.

Env: (none)
"""
import FreeCAD as App

asm = App.openDocument("/Users/cbrown/cbrown350-googledrive/workspaces/eink_weather/"
                       "work/cad/EInk_Weather_Display_Assembly.FCStd")
NAMES = [("PRINT_FRONT_BEZEL", "F"), ("PRINT_REAR_CHASSIS", "B"), ("PRINT_REAR_COVER", "C"),
         ("PRINT_SERVICE_HATCH", "H"), ("BOARD_PCB_48x66x1", "D"),
         ("BOARD_P2_USB_C", "U"), ("BOARD_P2_USB_C_Port", "P"),
         ("PANEL_ACTIVE_AREA", "G")]
S = {}
for n, ch in NAMES:
    o = asm.getObject(n)
    if o and hasattr(o, "Shape") and not o.Shape.isNull():
        S[ch] = o.Shape

print("USB-C port bbox:", asm.getObject("BOARD_P2_USB_C_Port").Shape.BoundBox)
print("USB-C shell bbox:", asm.getObject("BOARD_P2_USB_C").Shape.BoundBox)
print()
print("x=29.60 section. F=bezel B=chassis C=cover H=hatch D=pcb U=usb-shell P=usb-port G=panel")
print("       y: " + "".join("%d" % (int(v) % 10) for v in
                            [58 + 1.0 * i for i in range(50)]))
z = 20.0
while z >= -9.0:
    row = ""
    for i in range(50):
        y = 58 + 1.0 * i
        p = App.Vector(29.60, y, z)
        c = "."
        for ch, s in S.items():
            if s.isInside(p, 1e-6, True):
                c = ch
                break
        row += c
    print("  z=%6.2f %s" % (z, row))
    z -= 0.5
