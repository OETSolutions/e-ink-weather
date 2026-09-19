# Good Display ESP32-M1 — mechanical reconstruction for enclosure design

## Deliverables
- `GoodDisplay_ESP32_M1_mechanical.step`: multi-body STEP assembly for direct import into FreeCAD.
- `GoodDisplay_ESP32_M1_FreeCAD.FCMacro`: native FreeCAD macro that creates named Part solids plus case-clearance helpers.
- `GoodDisplay_ESP32_M1_locations.csv`: editable mechanical location table.
- `GoodDisplay_ESP32_M1_board_outline.dxf`: board outline for sketches / enclosure workflows.

## Coordinate system
Origin is the **bottom-left PCB corner when viewing the component side straight-on**. X goes right (0–48 mm), Y goes up (0–66 mm), and Z=0 is the PCB bottom. The official site writes the outside dimensions as **66 × 48 × 1.0 mm**; the model represents that as 48 mm X × 66 mm Y × 1.0 mm Z.

## Sourced facts
Good Display's current ESP32-M1 product page specifies:
- Outside dimensions: 66 × 48 × 1.0 mm.
- USB Type-C power/data.
- 3.7 V Li-ion battery connector, SH1.0.
- Power switch.
- microSD slot.
- User button.
- Brightness adjustment resistor (RT).
- Front-light voltage select and HV switch.
- Front-light FPC.
- e-Paper FPC, P5, 24-pin.
- Touch FPCs, P6/P7.
- RESE resistor-match switch, P4.
- RST and BOOT buttons.
- ESP32-WROOM-32D module.

Espressif's ESP32-WROOM-32D datasheet specifies the module at **18.00 × 25.50 × 3.10 mm** (nominal, with dimensional tolerances in the datasheet). That known component and the official 48 × 66 mm PCB were used as independent image-scale checks.

Official references:
- Good Display ESP32-M1 product page: https://www.good-display.com/product/1068.html
- Good Display ESP32-M1 schematic listing: https://www.good-display.com/companyfile/2058.html
- Espressif ESP32-WROOM-32D / 32U datasheet: https://documentation.espressif.com/esp32-wroom-32d_esp32-wroom-32u_datasheet_en.html

## What is inferred
Good Display does **not** publish a dimensioned mechanical drawing, board STEP file, or PCB layout/gerbers on the public product/download pages I found. Therefore:
- PCB **size and thickness** are sourced.
- WROOM-32D **body dimensions** are sourced.
- Connector/switch/button **identity and function** are sourced from Good Display.
- The **XY locations**, PCB corner radius, mounting-hole positions/diameters, and most peripheral component body dimensions are reconstructed from the official straight-on product photograph and standard package proportions.

The STEP/macro intentionally use simple bounding solids rather than pretending to know exact manufacturer part numbers for every connector. For a case, that is generally preferable: it exposes the wall-opening and interference geometry clearly.

## Location table summary
Dimensions below are the model values. For box solids, X/Y is lower-left. Cylinders use center X/Y.

| Item | X | Y | Size / dia | Z height | Confidence |
|---|---:|---:|---|---:|---|
| ESP32-WROOM-32D | 24.0 | 34.2 | 18.0 × 25.5 | 3.1 | High dimensions; image-derived XY |
| USB-C receptacle | -0.7 | 51.4 | 7.8 × 9.0 | 3.25 | Medium-high |
| Battery SH1.0 | 0.2 | 41.2 | 5.3 × 4.0 | 3.0 | Medium |
| Power switch body | 0.2 | 31.4 | 6.2 × 5.2 | 2.4 | Medium-high |
| Power actuator | -3.0 | 32.5 | 3.4 × 3.0 | 1.45 | Medium-high |
| microSD socket | 0.4 | 14.8 | 13.7 × 15.2 | 1.85 | Medium-high |
| User KEY | 4.6 | 7.8 | 4.6 × 4.6 | 2.4 | Medium-high |
| Brightness RT | 7.0 | 2.6 | Ø5.0 | 3.2 | Medium-high |
| P8 voltage select | 14.0 | 1.0 | 7.0 × 5.5 | 5.5 | Medium |
| SS1 HV switch | 21.0 | 5.8 | 5.4 × 3.4 | 2.4 | Medium |
| P9 front-light FPC | 22.5 | -0.2 | 7.0 × 4.2 | 2.1 | Medium-high |
| Touch P6 FPC | 29.8 | -0.2 | 8.0 × 4.2 | 2.1 | Medium |
| Touch P7 FPC | 43.6 | 25.5 | 4.6 × 8.0 | 2.2 | Medium |
| P5 24-pin e-paper FPC | 43.3 | 7.0 | 5.0 × 16.0 | 2.2 | Medium-high |
| P4 RESE body | 43.6 | 49.8 | 4.5 × 5.2 | 2.5 | Medium-high |
| P4 RESE actuator | 47.6 | 51.0 | 3.0 × 2.8 | 1.4 | Medium-high |
| RST button | 8.6 | 59.0 | 4.5 × 4.5 | 2.5 | Medium |
| BOOT button | 15.0 | 59.0 | 4.5 × 4.5 | 2.5 | Medium |
| Mounting holes | centers 3,3 and 45,63 | Ø3.2 | through | — | Medium-high |

## Enclosure use
The model includes a `Case_Clearances` group / `CLR_*` solids. These are deliberately larger than the nominal hardware and can be Boolean-subtracted from a case wall/cover. They cover USB-C, microSD card insertion, power switch access, RESE access, FPC exits, pushbutton access, and screwdriver access to the brightness trimmer.

For a one-off FDM enclosure, leave an additional **0.4–0.7 mm per side** around board-edge openings, depending on your printer/material. Do not make injection-mold tooling or a zero-clearance machined enclosure from the inferred dimensions without checking a physical board; the public information is not sufficient for that accuracy level.

## FreeCAD workflow
1. Open the STEP directly in FreeCAD for a fixed multi-body reference model; or
2. Copy `GoodDisplay_ESP32_M1_FreeCAD.FCMacro` into your FreeCAD macro directory and execute it. The macro creates named native `Part::Feature` objects and a `Case_Clearances` group.
3. Hide `Case_Clearances` for normal viewing; show it when creating wall cutouts.
4. Save the generated document as `.FCStd` after any dimension corrections measured from your actual board.
