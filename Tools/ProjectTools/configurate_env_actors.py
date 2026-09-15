"""
configurate_env_actors.py

Opens a simple interactive map window centered at a given latitude/longitude,
invoked from the Environment menu section's "ConfigurateEnvActorsBtn" button
(see AAirplane/UEnvironmentSectionWidget in the Unreal project) via
AerodynamicToolRunner::RunPythonScript, the same mechanism
AerodynamicPhysicalCalculationUtil::CalculatePolar uses to run Python tools.

On startup the script loads env_actors.json (if present) and re-draws every entry
already there - AEnvironmentActorManager::OpenConfigurationTool writes this file
from its current EWConfigurations array right before launching the script, so
whatever is already configured in Unreal shows up on the map immediately.

The window also lets the user place "Electronic warfare" (EW) markers on the map:
click "Add Electronic warfare", then click anywhere on the map to drop a red "EW"
marker (default height=0m, radius=50m) there. Clicking a marker opens a modal to
edit its Latitude/Longitude/Height/Radius, save the changes (moves/resizes the
marker), or delete it.

The "SAVE" button writes every object array (currently just "electronic_warfare")
to env_actors.json next to this script, keyed by object type so further arrays
(roads, buildings, ...) can be added later without breaking the format.
AEnvironmentActorManager::LoadConfigurationsFromFile reads it back once this
script's window is closed and (re)spawns AEWZoneActor accordingly.

Standalone - no dependency on map_object_marker.py.

Dependency:
    pip install tkintermapview

Usage:
    python configurate_env_actors.py <latitude> <longitude>

Example:
    python configurate_env_actors.py 50.4501 30.5234
"""

import json
import math
import os
import sys

# Unreal's embedded Python resolves Tcl/Tk's init.tcl from a path relative to the
# current working directory at the time Tk() is constructed, which can differ from
# the interpreter's own install location. Point TCL_LIBRARY/TK_LIBRARY at the
# interpreter's own tcl/tk folders explicitly so the lookup no longer depends on CWD.
_TCL_ROOT = os.path.join(sys.prefix, "tcl")
os.environ.setdefault("TCL_LIBRARY", os.path.join(_TCL_ROOT, "tcl8.6"))
os.environ.setdefault("TK_LIBRARY", os.path.join(_TCL_ROOT, "tk8.6"))

import tkinter as tk
from tkinter import messagebox

try:
    from tkintermapview import TkinterMapView
except ImportError:
    raise SystemExit("tkintermapview not found. Install it: pip install tkintermapview")

try:
    from PIL import Image, ImageDraw, ImageFont, ImageTk
except ImportError:
    raise SystemExit("Pillow not found. Install it: pip install Pillow")


START_ZOOM = 17
EARTH_RADIUS_M = 6378137.0
CIRCLE_POINT_COUNT = 48

EW_MARKER_SIZE = 36
EW_MARKER_FILL = (214, 39, 40, 255)
EW_MARKER_OUTLINE = (120, 0, 0, 255)
EW_ZONE_OUTLINE_COLOR = "#d62728"

DEFAULT_EW_HEIGHT = 0.0
DEFAULT_EW_RADIUS = 50.0

# Where SAVE writes the object arrays for Unreal to pick up later. Sits next to this
# script, same convention as map_object_marker.py's map_objects.json.
OUTPUT_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "env_actors.json")

# Google Maps tiles. lyrs options:
#   m  - plain map (roads)
#   s  - satellite
#   y  - hybrid (satellite + labels)
#   p  - terrain
GOOGLE_TILE_SERVER = "https://mt0.google.com/vt/lyrs=y&hl=en&x={x}&y={y}&z={z}&s=Ga"


def make_ew_icon(size: int = EW_MARKER_SIZE) -> "ImageTk.PhotoImage":
    """Builds a small red circle icon with 'EW' centered inside it."""
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.ellipse((1, 1, size - 2, size - 2), fill=EW_MARKER_FILL, outline=EW_MARKER_OUTLINE, width=2)

    text = "EW"
    font = None
    for font_name in ("arialbd.ttf", "Arial Bold.ttf", "DejaVuSans-Bold.ttf"):
        try:
            font = ImageFont.truetype(font_name, size=int(size * 0.34))
            break
        except OSError:
            continue
    if font is None:
        font = ImageFont.load_default()

    bbox = draw.textbbox((0, 0), text, font=font)
    text_w, text_h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    draw.text(((size - text_w) / 2 - bbox[0], (size - text_h) / 2 - bbox[1]), text, fill="white", font=font)

    return ImageTk.PhotoImage(image)


def circle_points(latitude: float, longitude: float, radius_m: float, num_points: int = CIRCLE_POINT_COUNT) -> list:
    """Approximates a geographic circle of the given radius (metres) around (latitude, longitude)."""
    lat_rad = math.radians(latitude)
    points = []
    for i in range(num_points + 1):
        angle = 2 * math.pi * i / num_points
        east_m = radius_m * math.cos(angle)
        north_m = radius_m * math.sin(angle)
        d_lat = (north_m / EARTH_RADIUS_M) * (180.0 / math.pi)
        d_lon = (east_m / (EARTH_RADIUS_M * max(math.cos(lat_rad), 1e-9))) * (180.0 / math.pi)
        points.append((latitude + d_lat, longitude + d_lon))
    return points


class ConfigurateEnvActorsApp:
    def __init__(self, root: tk.Tk, start_lat: float, start_lon: float):
        self.root = root
        self.ew_objects = {}   # id -> {"latitude", "longitude", "height", "radius", "marker", "zone"}
        self.next_ew_id = 0
        self.placing_ew = False
        self.ew_icon = make_ew_icon()

        root.title("Environment actors map")
        root.geometry("1000x700")

        panel = tk.Frame(root)
        panel.pack(side="top", fill="x")

        self.add_ew_button = tk.Button(panel, text="Add Electronic warfare", command=self.start_add_ew)
        self.add_ew_button.pack(side="left", padx=8, pady=6)

        self.status_var = tk.StringVar(value="")
        tk.Label(panel, textvariable=self.status_var, fg="#555555").pack(side="left", padx=8)

        self.save_button = tk.Button(panel, text="SAVE", command=self.on_save_clicked)
        self.save_button.pack(side="right", padx=8, pady=6)

        self.map_widget = TkinterMapView(root, width=1000, height=660, corner_radius=0)
        self.map_widget.pack(fill="both", expand=True)
        self.map_widget.set_tile_server(GOOGLE_TILE_SERVER, max_zoom=22)
        self.map_widget.set_position(start_lat, start_lon)
        self.map_widget.set_zoom(START_ZOOM)
        self.map_widget.add_left_click_map_command(self.on_map_left_click)

        self.load_existing_ew_objects()

    # ------------------------------------------------------------------ #
    # Load (pick up whatever Unreal last wrote/saved)
    # ------------------------------------------------------------------ #

    def load_existing_ew_objects(self):
        """
        Loads env_actors.json if present and re-creates its "electronic_warfare"
        entries on the map. AEnvironmentActorManager::OpenConfigurationTool writes
        this file (from its EWConfigurations array) right before launching this
        script, so whatever is already configured in Unreal shows up immediately.
        """
        if not os.path.exists(OUTPUT_FILE):
            return

        try:
            with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError):
            return

        for entry in data.get("electronic_warfare", []):
            try:
                latitude = float(entry["latitude"])
                longitude = float(entry["longitude"])
                height = float(entry.get("height", DEFAULT_EW_HEIGHT))
                radius = float(entry.get("radius", DEFAULT_EW_RADIUS))
            except (KeyError, TypeError, ValueError):
                continue
            self.add_ew_object(latitude, longitude, height, radius)

    # ------------------------------------------------------------------ #
    # Placement
    # ------------------------------------------------------------------ #

    def start_add_ew(self):
        self.placing_ew = True
        self.status_var.set("Click on the map to place the EW object...")

    def on_map_left_click(self, coordinate_tuple):
        if not self.placing_ew:
            return
        self.placing_ew = False
        self.status_var.set("")

        latitude, longitude = coordinate_tuple
        self.add_ew_object(latitude, longitude)

    def add_ew_object(self, latitude: float, longitude: float,
                       height: float = DEFAULT_EW_HEIGHT, radius: float = DEFAULT_EW_RADIUS) -> int:
        obj_id = self.next_ew_id
        self.next_ew_id += 1

        marker = self.map_widget.set_marker(
            latitude, longitude,
            icon=self.ew_icon, icon_anchor="center",
            command=self.on_ew_object_clicked, data=obj_id)

        zone = self.map_widget.set_polygon(
            circle_points(latitude, longitude, radius),
            outline_color=EW_ZONE_OUTLINE_COLOR, fill_color=None, border_width=2,
            command=self.on_ew_object_clicked, data=obj_id)

        self.ew_objects[obj_id] = {
            "latitude": latitude,
            "longitude": longitude,
            "height": height,
            "radius": radius,
            "marker": marker,
            "zone": zone,
        }
        return obj_id

    # ------------------------------------------------------------------ #
    # Editing
    # ------------------------------------------------------------------ #

    def on_ew_object_clicked(self, canvas_object):
        ew = self.ew_objects.get(canvas_object.data)
        if ew is not None:
            self.open_ew_modal(canvas_object.data, ew)

    def open_ew_modal(self, obj_id: int, ew: dict):
        modal = tk.Toplevel(self.root)
        modal.title(f"Electronic warfare #{obj_id}")
        modal.resizable(False, False)
        modal.transient(self.root)
        modal.grab_set()

        field_vars = {}

        def add_field(row, label, value):
            tk.Label(modal, text=label).grid(row=row, column=0, sticky="w", padx=10, pady=6)
            var = tk.StringVar(value=str(value))
            tk.Entry(modal, textvariable=var, width=20).grid(row=row, column=1, padx=10, pady=6)
            field_vars[label] = var

        add_field(0, "Latitude", ew["latitude"])
        add_field(1, "Longitude", ew["longitude"])
        add_field(2, "Height", ew["height"])
        add_field(3, "Radius", ew["radius"])

        button_row = tk.Frame(modal)
        button_row.grid(row=4, column=0, columnspan=2, pady=(4, 10))

        def on_save():
            try:
                latitude = float(field_vars["Latitude"].get())
                longitude = float(field_vars["Longitude"].get())
                height = float(field_vars["Height"].get())
                radius = float(field_vars["Radius"].get())
            except ValueError:
                messagebox.showerror("Invalid input", "Latitude, Longitude, Height and Radius must be numbers.",
                                      parent=modal)
                return

            self.update_ew_object(obj_id, latitude, longitude, height, radius)
            modal.destroy()

        def on_delete():
            self.delete_ew_object(obj_id)
            modal.destroy()

        def on_close():
            modal.destroy()

        tk.Button(button_row, text="Save", width=10, command=on_save).pack(side="left", padx=5)
        tk.Button(button_row, text="Close", width=10, command=on_close).pack(side="left", padx=5)
        tk.Button(button_row, text="Delete", width=10, command=on_delete).pack(side="left", padx=5)

    def update_ew_object(self, obj_id: int, latitude: float, longitude: float, height: float, radius: float):
        ew = self.ew_objects.get(obj_id)
        if ew is None:
            return

        ew["latitude"] = latitude
        ew["longitude"] = longitude
        ew["height"] = height
        ew["radius"] = radius

        ew["marker"].set_position(latitude, longitude)
        ew["zone"].position_list = circle_points(latitude, longitude, radius)
        ew["zone"].draw()

    def delete_ew_object(self, obj_id: int):
        ew = self.ew_objects.pop(obj_id, None)
        if ew is None:
            return

        ew["marker"].delete()
        ew["zone"].delete()

    # ------------------------------------------------------------------ #
    # Save (hand data back to Unreal)
    # ------------------------------------------------------------------ #

    def build_export_payload(self) -> dict:
        """
        Collects every object array into one JSON-serializable dict, keyed by
        object type. Only plain data is exported (canvas objects like markers
        and zones are runtime-only and left out). Add further object types
        (roads, buildings, ...) here as their own top-level keys as they're
        introduced.
        """
        return {
            "electronic_warfare": [
                {
                    "latitude": ew["latitude"],
                    "longitude": ew["longitude"],
                    "height": ew["height"],
                    "radius": ew["radius"],
                }
                for ew in self.ew_objects.values()
            ],
        }

    def on_save_clicked(self):
        payload = self.build_export_payload()
        with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
        self.status_var.set(f"Saved {len(payload['electronic_warfare'])} EW object(s) to {OUTPUT_FILE}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: configurate_env_actors.py <latitude> <longitude>")
        sys.exit(1)

    root = tk.Tk()
    ConfigurateEnvActorsApp(root, float(sys.argv[1]), float(sys.argv[2]))
    root.mainloop()
