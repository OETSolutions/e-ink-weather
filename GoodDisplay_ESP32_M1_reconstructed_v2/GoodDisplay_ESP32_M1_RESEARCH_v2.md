# Good Display ESP32-M1 mechanical reconstruction v2

## Coordinate system
- Units: mm.
- Origin: lower-left PCB corner when looking at the component side, with the USB-C on the left and the ESP32 module toward the upper-right.
- +X right, +Y up, +Z away from the component side.

## Authoritative dimensions
- Good Display publishes the ESP32-M1 outside dimension as **66 × 48 × 1.0 mm**. In this model it is represented as X=48, Y=66, Z=1.0.
- Espressif publishes the ESP32-WROOM-32D envelope as **18 × 25.5 × 3.1 mm**. The module placement is photo-scaled, but its envelope is authoritative.

## Reconstruction method
The official straight-on product image is essentially orthographic. The PCB edge was used to map pixels to 48 × 66 mm independently in X and Y. The ESP32-WROOM-32D outline was then used as a scale sanity check; its photo footprint agrees with 18 × 25.5 mm closely enough to support sub-mm placement work for the large interfaces.

The model specifically reconstructs all enclosure-relevant hardware visible in the official image:
- four mounting holes
- P2 USB-C receptacle and port direction
- RST and BOOT pushbuttons
- P4 RESE slide switch and actuator direction
- P1 SH1.0 battery connector
- left-edge ON/OFF slide switch and actuator
- U4 microSD socket and card insertion direction
- S1 KEY side button
- RT front-light brightness trimmer
- P8 front-light voltage selector
- SS1 front-light HV DIP switch
- P6 front-light FPC
- TP1 and TP2 touch FPC connectors
- P5 24-pin e-paper FPC
- ESP32-WROOM-32D
- several major IC/inductor/passive landmarks so the model visually tracks the real board

## Accuracy / limitations
Good Display does not publish the PCB Gerbers, a dimensioned mechanical drawing, or an ESP32-M1 STEP file on the product page. Therefore the board outer size and WROOM module size are sourced dimensions, while mounting-hole diameter/centers and the peripheral component XY envelopes are image-derived. The official schematic identifies functions and reference designators but does not provide mechanical footprints.

For enclosure work, the large edge components are reconstructed directly from the straight-on photo rather than from generic guessed board positions. Expect image-derived XY uncertainty on the order of roughly 0.3–0.8 mm, depending on edge definition. Before committing to a tight molded enclosure, verify critical opening dimensions against a physical board. For a 3D-printed case with ~0.5–1.0 mm clearance, this reconstruction is intended to be directly useful.

## Files
- `GoodDisplay_ESP32_M1_reconstructed.step` — multi-part STEP assembly.
- `GoodDisplay_ESP32_M1_reconstructed.FCMacro` — creates native named FreeCAD objects and hidden enclosure keep-out solids.
- `GoodDisplay_ESP32_M1_locations_v2.csv` — XY envelopes and basis for all case-relevant components.
- `GoodDisplay_ESP32_M1_v2_preview.png` — top-view placement check.

## FreeCAD use
Open FreeCAD, choose **Macro → Macros → Create**, paste/import the `.FCMacro`, then run it. The generated document contains named solids. The `CASE_KEEPOUTS` group contains hidden clearance volumes for edge cutouts; toggle visibility and use Boolean Cut against your enclosure shell.
