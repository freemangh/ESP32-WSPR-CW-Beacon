# PCB Design Specifications - ESP32 WSPR Beacon

## 1. Schematic Connections

### Main Controller (ESP32 Dev Board)
- **3V3**: Connect to Display VCC.
- **GND**: Connect to Display GND, LPF Ground, RF Connector Ground.
- **GPIO 21 (SDA)**: Connect to Display SDA.
- **GPIO 22 (SCL)**: Connect to Display SCL.
- **GPIO 27**: **RF SOURCE**. Connect to LPF Input.

### Low Pass Filter (LPF) Network
*Note: Layout should be linear (Input -> L1 -> L2 -> L3 -> Output)*
1. **Input Node**:
   - Connect **ESP32 GPIO 27**.
   - Connect **C1 (470pF)** to GND.
   - Connect **L1 (0.6uH)** Pin 1.
2. **Node A (Between L1/L2)**:
   - Connect **L1** Pin 2.
   - Connect **C2 (1000pF)** to GND.
   - Connect **L2 (0.7uH)** Pin 1.
3. **Node B (Between L2/L3)**:
   - Connect **L2** Pin 2.
   - Connect **C3 (1000pF)** to GND.
   - Connect **L3 (0.6uH)** Pin 1.
4. **Output Node**:
   - Connect **L3** Pin 2.
   - Connect **C4 (470pF)** to GND.
   - Connect **RF Connector** Center Pin.

### Peripherals
- **OLED Display**:
  - VCC -> 3V3
  - GND -> GND
  - SDA -> GPIO 21
  - SCL -> GPIO 22

## 2. Layout Guidelines
1. **RF Trace**: Keep the trace from GPIO 27 to LPF and LPF to Antenna short and thick (e.g., 0.5mm+).
2. **Ground Plane**: Use a solid Ground Pour on bottom (and top if possible). Stitch them with vias.
3. **LPF**:
   - Place Capacitors (C1-C4) close to the inductors.
   - Ensure specific Ground Vias for each capacitor component to the ground plane.
   - Keep the LPF components (Inductors) slightly spaced to avoid magnetic coupling (or place at 90 degrees if very close).
4. **Antenna Connector**: Place at board edge. Ensure good grounding on the shield pins.

## 3. Mounting
- Add M3 mounting holes at corners.
- Dimensions recommended: 70mm x 50mm (Approx).
