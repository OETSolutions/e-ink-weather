"""Build a native named FreeCAD document from the project's authoritative reconstruction
macro, omitting only its GUI view command. Geometry is unchanged from the supplied macro.
"""
import FreeCAD as App
import os

HERE = os.path.dirname(os.path.abspath(__file__))
MACRO = os.path.abspath(os.path.join(HERE, "..", "..", "GoodDisplay_ESP32_M1_reconstructed_v2",
                                     "GoodDisplay_ESP32_M1_reconstructed.FCMacro"))
OUT = os.path.join(HERE, "esp32_m1.FCStd")

src = open(MACRO).read()
src = "\n".join(line for line in src.splitlines()
                if "Gui.activeDocument()" not in line)
# The source macro assigns GUI-only color/transparency properties. freecadcmd has no
# ViewObject; remove only those assignments, preserving every geometric operation.
src = src.replace("; o.ViewObject.ShapeColor=color", "")
src = src.replace("o.ViewObject.Transparency=75; o.ViewObject.Visibility=False; ", "")
exec(compile(src, MACRO, "exec"), globals(), globals())
doc.recompute()
doc.saveAs(OUT)

required = ["PCB_48x66x1", "P5_EPAPER_24PIN_FPC", "P5_ENTRY", "P2_USB_C",
            "U4_MICRO_SD_SOCKET", "POWER_SWITCH", "S1_KEY"]
print("document:", doc.Name, "objects:", len(doc.Objects))
for name in required:
    o = doc.getObject(name)
    if not o:
        raise RuntimeError("missing required object " + name)
    b = o.Shape.BoundBox
    print("  %-26s X %6.2f..%6.2f Y %6.2f..%6.2f Z %5.2f..%5.2f" %
          (name, b.XMin, b.XMax, b.YMin, b.YMax, b.ZMin, b.ZMax))
print("saved", OUT)
