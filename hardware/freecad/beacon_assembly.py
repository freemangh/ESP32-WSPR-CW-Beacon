import FreeCAD as App
import Part

doc_name = "Beacon_Assembly"
try:
    doc = App.getDocument(doc_name)
    if not doc:
        doc = App.newDocument(doc_name)
except:
    doc = App.newDocument(doc_name)

# --- Dimensions ---
pcb_width = 70.0
pcb_depth = 50.0
pcb_thick = 1.6

esp_width = 28.0
esp_depth = 54.0 # Slightly larger than board? Let's assume ESP32 hangs off or board is bigger. 
# Esp32 Devkit V1 is approx 52x28. 
# Let's resize PCB to confirm.
pcb_details_width = 55.0 
pcb_details_depth = 80.0 # Make board bigger to fit everything
pcb_width = 55.0
pcb_depth = 80.0

# --- Helper to create box ---
def make_box(name, length, width, height, x, y, z, color=(0.8, 0.8, 0.8)):
    obj = doc.addObject("Part::Box", name)
    obj.Length = length
    obj.Width = width
    obj.Height = height
    obj.Placement = App.Placement(App.Vector(x, y, z), App.Rotation(0, 0, 0))
    if hasattr(obj, "ViewObject"):
        obj.ViewObject.ShapeColor = color
    return obj

# --- Helper to create cylinder ---
def make_cyl(name, radius, height, x, y, z, color=(0.8, 0.8, 0.8)):
    obj = doc.addObject("Part::Cylinder", name)
    obj.Radius = radius
    obj.Height = height
    obj.Placement = App.Placement(App.Vector(x, y, z), App.Rotation(0, 0, 0))
    if hasattr(obj, "ViewObject"):
        obj.ViewObject.ShapeColor = color
    return obj

# 1. PCB
pcb = make_box("PCB", pcb_width, pcb_depth, pcb_thick, 0, 0, 0, (0.0, 0.5, 0.0))

# 2. ESP32 Module (Placed at top)
esp = make_box("ESP32", 28.0, 52.0, 13.0, (pcb_width-28)/2, pcb_depth - 54.0, pcb_thick, (0.2, 0.2, 0.2))

# 3. OLED Display (Placed below ESP32)
oled = make_box("OLED", 27.0, 27.0, 4.0, (pcb_width-27)/2, pcb_depth - 54.0 - 30.0, pcb_thick, (0.1, 0.1, 0.8))

# 4. SMA Connector (Bottom edge)
sma_body = make_box("SMA_Body", 6.0, 6.0, 6.0, (pcb_width-6)/2, 0, pcb_thick, (0.8, 0.6, 0.1))
sma_pin = make_cyl("SMA_Pin", 2.0, 10.0, pcb_width/2, -5.0, pcb_thick+3, (0.8, 0.6, 0.1))
# Rotate pin to point out? Actually cylinder defaults to Z up. 
# Let's rotate it 90 deg X.
sma_pin.Placement = App.Placement(App.Vector(pcb_width/2, 3.0, pcb_thick+3), App.Rotation(App.Vector(1,0,0), 90))


# 5. LPF Components (Between OLED and SMA)
lpf_start_y = 20.0
# Inductors (Toroids - simplified as cylinders)
l1 = make_cyl("L1", 4.0, 3.0, pcb_width/2 - 10, lpf_start_y, pcb_thick, (0.8, 0.8, 0.0))
l2 = make_cyl("L2", 4.0, 3.0, pcb_width/2, lpf_start_y, pcb_thick, (0.8, 0.8, 0.0))
l3 = make_cyl("L3", 4.0, 3.0, pcb_width/2 + 10, lpf_start_y, pcb_thick, (0.8, 0.8, 0.0))

# Capacitors (Small boxes)
c1 = make_box("C1", 2.0, 1.2, 1.0, pcb_width/2 - 15, lpf_start_y, pcb_thick, (0.6, 0.4, 0.2))
c2 = make_box("C2", 2.0, 1.2, 1.0, pcb_width/2 - 5, lpf_start_y, pcb_thick, (0.6, 0.4, 0.2))
c3 = make_box("C3", 2.0, 1.2, 1.0, pcb_width/2 + 5, lpf_start_y, pcb_thick, (0.6, 0.4, 0.2))
c4 = make_box("C4", 2.0, 1.2, 1.0, pcb_width/2 + 15, lpf_start_y, pcb_thick, (0.6, 0.4, 0.2))

App.ActiveDocument.recompute()
Gui.SendMsgToActiveView("ViewFit")
