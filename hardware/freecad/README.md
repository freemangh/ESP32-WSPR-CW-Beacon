# FreeCAD Beacon Assembly

This directory contains resources to generate a 3D Assembly of the ESP32 Beacon in FreeCAD.

## How to use
1. Open **FreeCAD**.
2. Go to **View -> Panels -> Python Console** (Ensure it is visible).
3. Open the `beacon_assembly.py` file in a text editor.
4. Copy the entire content of `beacon_assembly.py`.
5. Paste it into the **Python Console** in FreeCAD and press Enter.

## Result
The script will generate a new document named `Beacon_Assembly` containing:
- The PCB Substrate (Green)
- ESP32 Development Board (Generic representation)
- OLED Display
- SMA Connector
- LPF Components (Inductors and Capacitors)

You can then export this as STEP or STL for enclosure design.
