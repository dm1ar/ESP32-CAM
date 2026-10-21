# ESP32-CAM Housing with Magnetic Ball Mount and Halbach Base

This project is a modified and enhanced fork of [laisch’s ESP32 Cam housing](https://makerworld.com/en/models/21133-esp32-cam-housing). Many thanks to laisch for the original design, which provided the starting point for this version.

The housing has been redesigned to accommodate an M11 USB-C panel connector and an adjustable magnetic ball mount. A five-magnet Halbach array provides the magnetic mounting base.

The main changes include:

- Extended housing with space for a RUNCCI-YUN M11 USB-C connector and its retaining nut.
- Reinforced, 2.4 mm thick USB mounting wall.
- Deeper housing base for nut installation, while retaining the PCB position.
- Centered wedge latches along both long sides.
- A 12 mm ball mount with a reinforced 5 mm neck.
- Two M3 heat-set inserts for attaching the ball mount.
- Magnetic clamp halves with two magnets each.
- Five Halbach-array magnet pockets with engraved orientation guidance.
- Ø2 mm push-out holes for removing the magnets.
- Enlarged backside lettering and attribution.

## Parts required

Print one of each part in PLA:

1. Housing Base
2. Housing Cover with LED Window — supplied as one combined part
3. Camera Ball Mount
4. Magnetic Clamp Base
5. Magnetic Clamp Cover

Additional hardware:

| Component                                                 | Quantity |
| --------------------------------------------------------- | --------:|
| ESP32-CAM module with camera                              | 1        |
| RUNCCI-YUN M11 USB-C panel-mount connector, including nut | 1        |
| M3 heat-set inserts, nominal Ø5 × 4 mm                    | 2        |
| M3 × 8 mm screws                                          | 2        |
| 5 × 5 × 5 mm magnet cubes                                 | 9        |

The specified magnets are [supermagnete W-05-N](https://www.supermagnete.de/wuerfelmagnete-neodym/wuerfelmagnet-5mm_W-05-N): nickel-plated NdFeB cubes, grade N42. Five are used in the Halbach mounting array and four in the ball clamp. They are sold in packs of ten, so one pack covers one assembly.

## Assembly

1. **Prepare the printed parts.**
   Remove any print residue and check the housing latches, ball joint, and magnet pockets. Test-fit one magnet before installing the complete set.

2. **Install the threaded inserts.**
   Fit the two M3 heat-set inserts into the Housing Base from the underside. Keep them straight and allow the plastic to cool before inserting the screws.

3. **Install the USB-C connector.**
   Pass the connector through the Ø11 mm opening in the base and secure it with its hex nut from inside. Install the connector before the PCB so the nut remains accessible. The cover has a matching Ø14 mm clearance opening.

4. **Connect and fit the electronics.**
   Connect the USB-C power leads according to your ESP32-CAM board’s voltage requirements and pin markings. Check polarity before powering the board. Seat the PCB in its printed supports and route the wires clear of the cover latches.

5. **Attach the ball mount.**
   Secure the Camera Ball Mount to the underside of the housing using the two M3 × 8 mm screws.

6. **Install the magnets.**
   Fit five cubes into the Halbach-array pockets, following the engraved orientation diagram. Fit two magnets into each clamp half, checking that the corresponding pairs attract when the halves close. Confirm every magnet’s orientation before using adhesive. Keep the push-out holes clear.

7. **Assemble the ball clamp.**
   Position the Magnetic Clamp Base around the ball and fit the Magnetic Clamp Cover. Check that the halves seat correctly and that the camera can be aimed as intended.

8. **Close the housing.**
   Align the cover and press the long-side latches together evenly, making sure no wires are trapped.

The download includes the printable STL parts, editable FreeCAD model, STEP assembly, and BOM. Print tolerances and final holding strength should be checked with your own printer, magnets, and mounting surface.
