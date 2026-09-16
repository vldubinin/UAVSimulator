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

It also lets the user place wind vectors: click "Add Wind", then click twice on
the map - the first click sets the vector's start point, the second its end
point. A blue arrow (line + rotated arrowhead) is drawn from start to end, plus
a rectangle around it - the arrow is the rectangle's long axis, and Radius
(default 50m) is how far the rectangle extends to either side of it (so its
width is 2*Radius). Clicking the arrow or the rectangle opens a modal with a
Start group (Latitude/Longitude/Height), an End group (Latitude/Longitude/
Height), Speed (m/s) and Radius (m) fields, a small side-view canvas that
redraws live as the Height fields change (showing the vector's vertical tilt),
and Save/Close/Delete buttons - same interaction pattern as EW.

The "SAVE" button writes every object array ("electronic_warfare", "wind") to
env_actors.json next to this script, keyed by object type so further arrays
(roads, buildings, ...) can be added later without breaking the format.
AEnvironmentActorManager::LoadConfigurationsFromFile reads both arrays back
once this script's window is closed and (re)spawns AEWZoneActor / AWindActor
accordingly.

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

WIND_ARROW_SIZE = 30
WIND_ARROW_FILL = (31, 119, 180, 255)
WIND_ARROW_OUTLINE = (10, 60, 100, 255)
WIND_LINE_COLOR = "#1f77b4"
WIND_LINE_WIDTH = 4
WIND_ZONE_OUTLINE_COLOR = "#1f77b4"

DEFAULT_WIND_HEIGHT = 0.0
DEFAULT_WIND_SPEED = 5.0
DEFAULT_WIND_RADIUS = 50.0

SIDE_VIEW_WIDTH = 240
SIDE_VIEW_HEIGHT = 110

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


def bearing_deg(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Compass heading (degrees, clockwise from north) from (lat1, lon1) to (lat2, lon2)."""
    lat1_rad, lat2_rad = math.radians(lat1), math.radians(lat2)
    d_lon = math.radians(lon2 - lon1)
    x = math.sin(d_lon) * math.cos(lat2_rad)
    y = math.cos(lat1_rad) * math.sin(lat2_rad) - math.sin(lat1_rad) * math.cos(lat2_rad) * math.cos(d_lon)
    return (math.degrees(math.atan2(x, y)) + 360.0) % 360.0


def make_wind_arrow_icon(heading_deg: float, size: int = WIND_ARROW_SIZE) -> "ImageTk.PhotoImage":
    """Builds a small blue triangular arrowhead icon, pre-rotated to point along heading_deg."""
    base = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(base)

    tip = (size * 0.5, size * 0.06)
    left = (size * 0.2, size * 0.9)
    right = (size * 0.8, size * 0.9)
    mid_back = (size * 0.5, size * 0.62)
    draw.polygon([tip, right, mid_back, left], fill=WIND_ARROW_FILL, outline=WIND_ARROW_OUTLINE)

    # base image points north (up); rotate clockwise by the heading to match it.
    rotated = base.rotate(-heading_deg, resample=Image.BICUBIC, expand=False)
    return ImageTk.PhotoImage(rotated)


def draw_side_view(canvas: "tk.Canvas", start_height: float, end_height: float):
    """Redraws the side-view canvas: a small arrow from (0, start_height) to (L, end_height)."""
    canvas.delete("all")
    width, height = SIDE_VIEW_WIDTH, SIDE_VIEW_HEIGHT
    margin_x, margin_y = 24, 16

    min_h = min(0.0, start_height, end_height)
    max_h = max(0.0, start_height, end_height)
    if max_h - min_h < 1e-6:
        min_h -= 1.0
        max_h += 1.0
    pad = (max_h - min_h) * 0.15
    min_h -= pad
    max_h += pad

    def map_y(h):
        t = (h - min_h) / (max_h - min_h)
        return height - margin_y - t * (height - 2 * margin_y)

    ground_y = map_y(0.0)
    canvas.create_line(margin_x, ground_y, width - margin_x, ground_y, fill="#aaaaaa", dash=(3, 2))
    canvas.create_text(4, ground_y, text="0m", anchor="w", fill="#888888", font=("Segoe UI", 7))

    x0, x1 = margin_x, width - margin_x
    y0, y1 = map_y(start_height), map_y(end_height)

    canvas.create_line(x0, y0, x1, y1, fill=WIND_LINE_COLOR, width=3, arrow=tk.LAST, arrowshape=(10, 12, 4))
    canvas.create_oval(x0 - 3, y0 - 3, x0 + 3, y0 + 3, fill=WIND_LINE_COLOR, outline="")

    canvas.create_text(x0, max(10, y0 - 8), text=f"{start_height:g}m", fill=WIND_LINE_COLOR, font=("Segoe UI", 8))
    canvas.create_text(x1, max(10, y1 - 8), text=f"{end_height:g}m", fill=WIND_LINE_COLOR, font=("Segoe UI", 8))


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


def offset_point(latitude: float, longitude: float, heading_deg: float, distance_m: float) -> tuple:
    """Moves (latitude, longitude) by distance_m metres along compass heading heading_deg."""
    lat_rad = math.radians(latitude)
    heading_rad = math.radians(heading_deg)
    east_m = distance_m * math.sin(heading_rad)
    north_m = distance_m * math.cos(heading_rad)
    d_lat = (north_m / EARTH_RADIUS_M) * (180.0 / math.pi)
    d_lon = (east_m / (EARTH_RADIUS_M * max(math.cos(lat_rad), 1e-9))) * (180.0 / math.pi)
    return latitude + d_lat, longitude + d_lon


def wind_rectangle_points(start_latitude: float, start_longitude: float,
                           end_latitude: float, end_longitude: float, radius_m: float) -> list:
    """
    A rectangle around the Start->End segment: the segment is its long axis, radius_m is
    how far the rectangle extends to the left and right of that axis (so its width is
    2*radius_m). Returns a closed ring of 5 (lat, lon) points for set_polygon.
    """
    heading = bearing_deg(start_latitude, start_longitude, end_latitude, end_longitude)
    right, left = heading + 90.0, heading - 90.0

    start_left = offset_point(start_latitude, start_longitude, left, radius_m)
    start_right = offset_point(start_latitude, start_longitude, right, radius_m)
    end_left = offset_point(end_latitude, end_longitude, left, radius_m)
    end_right = offset_point(end_latitude, end_longitude, right, radius_m)

    return [start_left, end_left, end_right, start_right, start_left]


class ConfigurateEnvActorsApp:
    def __init__(self, root: tk.Tk, start_lat: float, start_lon: float):
        self.root = root
        self.ew_objects = {}   # id -> {"latitude", "longitude", "height", "radius", "marker", "zone"}
        self.next_ew_id = 0
        self.placing_ew = False
        self.ew_icon = make_ew_icon()

        # id -> {"start_latitude", "start_longitude", "start_height",
        #        "end_latitude", "end_longitude", "end_height", "speed",
        #        "path", "arrow_marker", "arrow_icon"}
        self.wind_objects = {}
        self.next_wind_id = 0
        self.placing_wind = False
        self.wind_click_start = None   # (latitude, longitude) of the first of the two placement clicks

        root.title("Environment actors map")
        root.geometry("1000x700")

        panel = tk.Frame(root)
        panel.pack(side="top", fill="x")

        self.add_ew_button = tk.Button(panel, text="Add Electronic warfare", command=self.start_add_ew)
        self.add_ew_button.pack(side="left", padx=8, pady=6)

        self.add_wind_button = tk.Button(panel, text="Add Wind", command=self.start_add_wind)
        self.add_wind_button.pack(side="left", padx=8, pady=6)

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

        self.load_existing_objects()

    # ------------------------------------------------------------------ #
    # Load (pick up whatever Unreal last wrote/saved)
    # ------------------------------------------------------------------ #

    def load_existing_objects(self):
        """
        Loads env_actors.json if present and re-creates its "electronic_warfare" and
        "wind" entries on the map. AEnvironmentActorManager::OpenConfigurationTool
        writes this file (from its current EWConfigurations/WindConfigurations arrays)
        right before launching this script, so whatever is already configured in
        Unreal shows up immediately.
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

        for entry in data.get("wind", []):
            try:
                start_latitude = float(entry["start_latitude"])
                start_longitude = float(entry["start_longitude"])
                end_latitude = float(entry["end_latitude"])
                end_longitude = float(entry["end_longitude"])
                start_height = float(entry.get("start_height", DEFAULT_WIND_HEIGHT))
                end_height = float(entry.get("end_height", DEFAULT_WIND_HEIGHT))
                speed = float(entry.get("speed", DEFAULT_WIND_SPEED))
                radius = float(entry.get("radius", DEFAULT_WIND_RADIUS))
            except (KeyError, TypeError, ValueError):
                continue
            self.add_wind_object(start_latitude, start_longitude, end_latitude, end_longitude,
                                  start_height, end_height, speed, radius)

    # ------------------------------------------------------------------ #
    # Placement
    # ------------------------------------------------------------------ #

    def start_add_ew(self):
        self.placing_wind = False
        self.wind_click_start = None
        self.placing_ew = True
        self.status_var.set("Click on the map to place the EW object...")

    def start_add_wind(self):
        self.placing_ew = False
        self.placing_wind = True
        self.wind_click_start = None
        self.status_var.set("Click on the map to place the START of the wind vector...")

    def on_map_left_click(self, coordinate_tuple):
        if self.placing_ew:
            self.placing_ew = False
            self.status_var.set("")

            latitude, longitude = coordinate_tuple
            self.add_ew_object(latitude, longitude)
            return

        if self.placing_wind:
            latitude, longitude = coordinate_tuple

            if self.wind_click_start is None:
                self.wind_click_start = (latitude, longitude)
                self.status_var.set("Click on the map to place the END of the wind vector...")
            else:
                start_latitude, start_longitude = self.wind_click_start
                self.wind_click_start = None
                self.placing_wind = False
                self.status_var.set("")
                self.add_wind_object(start_latitude, start_longitude, latitude, longitude)

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

    def add_wind_object(self, start_latitude: float, start_longitude: float,
                         end_latitude: float, end_longitude: float,
                         start_height: float = DEFAULT_WIND_HEIGHT, end_height: float = DEFAULT_WIND_HEIGHT,
                         speed: float = DEFAULT_WIND_SPEED, radius: float = DEFAULT_WIND_RADIUS) -> int:
        obj_id = self.next_wind_id
        self.next_wind_id += 1

        heading = bearing_deg(start_latitude, start_longitude, end_latitude, end_longitude)
        arrow_icon = make_wind_arrow_icon(heading)

        # The rectangle is drawn first so the arrow (path + arrowhead) renders on top of it.
        zone = self.map_widget.set_polygon(
            wind_rectangle_points(start_latitude, start_longitude, end_latitude, end_longitude, radius),
            outline_color=WIND_ZONE_OUTLINE_COLOR, fill_color=None, border_width=2,
            command=self.on_wind_object_clicked, data=obj_id)

        path = self.map_widget.set_path(
            [(start_latitude, start_longitude), (end_latitude, end_longitude)],
            color=WIND_LINE_COLOR, width=WIND_LINE_WIDTH,
            command=self.on_wind_object_clicked, data=obj_id)

        arrow_marker = self.map_widget.set_marker(
            end_latitude, end_longitude,
            icon=arrow_icon, icon_anchor="center",
            command=self.on_wind_object_clicked, data=obj_id)

        self.wind_objects[obj_id] = {
            "start_latitude": start_latitude,
            "start_longitude": start_longitude,
            "start_height": start_height,
            "end_latitude": end_latitude,
            "end_longitude": end_longitude,
            "end_height": end_height,
            "speed": speed,
            "radius": radius,
            "zone": zone,
            "path": path,
            "arrow_marker": arrow_marker,
            "arrow_icon": arrow_icon,  # keep a reference alive - tkinter PhotoImages need one
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

    def on_wind_object_clicked(self, canvas_object):
        wind = self.wind_objects.get(canvas_object.data)
        if wind is not None:
            self.open_wind_modal(canvas_object.data, wind)

    def open_wind_modal(self, obj_id: int, wind: dict):
        modal = tk.Toplevel(self.root)
        modal.title(f"Wind vector #{obj_id}")
        modal.resizable(False, False)
        modal.transient(self.root)
        modal.grab_set()

        field_vars = {}

        def add_field(parent, row, label, value):
            tk.Label(parent, text=label).grid(row=row, column=0, sticky="w", padx=10, pady=4)
            var = tk.StringVar(value=str(value))
            tk.Entry(parent, textvariable=var, width=14).grid(row=row, column=1, padx=10, pady=4)
            field_vars[label] = var
            return var

        start_group = tk.LabelFrame(modal, text="Start")
        start_group.grid(row=0, column=0, padx=10, pady=(10, 4), sticky="nsew")
        add_field(start_group, 0, "Start Latitude", wind["start_latitude"])
        add_field(start_group, 1, "Start Longitude", wind["start_longitude"])
        start_height_var = add_field(start_group, 2, "Start Height", wind["start_height"])

        end_group = tk.LabelFrame(modal, text="End")
        end_group.grid(row=0, column=1, padx=10, pady=(10, 4), sticky="nsew")
        add_field(end_group, 0, "End Latitude", wind["end_latitude"])
        add_field(end_group, 1, "End Longitude", wind["end_longitude"])
        end_height_var = add_field(end_group, 2, "End Height", wind["end_height"])

        speed_row = tk.Frame(modal)
        speed_row.grid(row=1, column=0, columnspan=2, sticky="w", padx=10, pady=(4, 4))
        tk.Label(speed_row, text="Speed (m/s)").pack(side="left")
        speed_var = tk.StringVar(value=str(wind["speed"]))
        tk.Entry(speed_row, textvariable=speed_var, width=10).pack(side="left", padx=8)
        tk.Label(speed_row, text="Radius (m)").pack(side="left", padx=(12, 0))
        radius_var = tk.StringVar(value=str(wind["radius"]))
        tk.Entry(speed_row, textvariable=radius_var, width=10).pack(side="left", padx=8)

        side_view_group = tk.LabelFrame(modal, text="Side view (height tilt)")
        side_view_group.grid(row=2, column=0, columnspan=2, padx=10, pady=(4, 4), sticky="nsew")
        side_canvas = tk.Canvas(side_view_group, width=SIDE_VIEW_WIDTH, height=SIDE_VIEW_HEIGHT,
                                 bg="white", highlightthickness=1, highlightbackground="#cccccc")
        side_canvas.pack(padx=6, pady=6)

        def refresh_side_view(*_args):
            try:
                start_h = float(start_height_var.get())
                end_h = float(end_height_var.get())
            except ValueError:
                return
            draw_side_view(side_canvas, start_h, end_h)

        start_height_var.trace_add("write", refresh_side_view)
        end_height_var.trace_add("write", refresh_side_view)
        refresh_side_view()

        button_row = tk.Frame(modal)
        button_row.grid(row=3, column=0, columnspan=2, pady=(4, 10))

        def on_save():
            try:
                start_latitude = float(field_vars["Start Latitude"].get())
                start_longitude = float(field_vars["Start Longitude"].get())
                start_height = float(start_height_var.get())
                end_latitude = float(field_vars["End Latitude"].get())
                end_longitude = float(field_vars["End Longitude"].get())
                end_height = float(end_height_var.get())
                speed = float(speed_var.get())
                radius = float(radius_var.get())
            except ValueError:
                messagebox.showerror("Invalid input", "All fields must be numbers.", parent=modal)
                return

            self.update_wind_object(obj_id, start_latitude, start_longitude, start_height,
                                     end_latitude, end_longitude, end_height, speed, radius)
            modal.destroy()

        def on_delete():
            self.delete_wind_object(obj_id)
            modal.destroy()

        def on_close():
            modal.destroy()

        tk.Button(button_row, text="Save", width=10, command=on_save).pack(side="left", padx=5)
        tk.Button(button_row, text="Close", width=10, command=on_close).pack(side="left", padx=5)
        tk.Button(button_row, text="Delete", width=10, command=on_delete).pack(side="left", padx=5)

    def update_wind_object(self, obj_id: int, start_latitude: float, start_longitude: float, start_height: float,
                            end_latitude: float, end_longitude: float, end_height: float, speed: float,
                            radius: float):
        wind = self.wind_objects.get(obj_id)
        if wind is None:
            return

        wind["start_latitude"] = start_latitude
        wind["start_longitude"] = start_longitude
        wind["start_height"] = start_height
        wind["end_latitude"] = end_latitude
        wind["end_longitude"] = end_longitude
        wind["end_height"] = end_height
        wind["speed"] = speed
        wind["radius"] = radius

        wind["path"].set_position_list([(start_latitude, start_longitude), (end_latitude, end_longitude)])

        wind["zone"].position_list = wind_rectangle_points(start_latitude, start_longitude,
                                                             end_latitude, end_longitude, radius)
        wind["zone"].draw()

        heading = bearing_deg(start_latitude, start_longitude, end_latitude, end_longitude)
        wind["arrow_icon"] = make_wind_arrow_icon(heading)
        wind["arrow_marker"].change_icon(wind["arrow_icon"])
        wind["arrow_marker"].set_position(end_latitude, end_longitude)

    def delete_wind_object(self, obj_id: int):
        wind = self.wind_objects.pop(obj_id, None)
        if wind is None:
            return

        wind["zone"].delete()
        wind["path"].delete()
        wind["arrow_marker"].delete()

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
            "wind": [
                {
                    "start_latitude": wind["start_latitude"],
                    "start_longitude": wind["start_longitude"],
                    "start_height": wind["start_height"],
                    "end_latitude": wind["end_latitude"],
                    "end_longitude": wind["end_longitude"],
                    "end_height": wind["end_height"],
                    "speed": wind["speed"],
                    "radius": wind["radius"],
                }
                for wind in self.wind_objects.values()
            ],
        }

    def on_save_clicked(self):
        payload = self.build_export_payload()
        with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)
        self.status_var.set(
            f"Saved {len(payload['electronic_warfare'])} EW object(s) and "
            f"{len(payload['wind'])} wind vector(s) to {OUTPUT_FILE}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: configurate_env_actors.py <latitude> <longitude>")
        sys.exit(1)

    root = tk.Tk()
    ConfigurateEnvActorsApp(root, float(sys.argv[1]), float(sys.argv[2]))
    root.mainloop()
