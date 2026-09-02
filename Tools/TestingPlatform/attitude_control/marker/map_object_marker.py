"""
map_object_marker.py

Interactive Google Maps canvas for marking objects (building / road / bridge /
industrial / commercial / water / tree / car / other).

What the script does:
1. On start it opens the map at the coordinates of the first object in
   map_objects.json; if there are no objects it asks for latitude and longitude.
2. Opens a window with the Google map at the given coordinates.
3. The "+" button -> a dropdown with the list of object types (building, road,
   bridge, industrial, commercial, water, tree, car, other). The "b" key is a
   shortcut that instantly picks "building" without opening the dropdown.
4. After picking a type, either:
   a. click 4 points (corners) on the map; every click becomes a separate named
      bbox corner. First the two extreme points by longitude go to x_min / x_max,
      the remaining two by latitude go to y_min / y_max. Each corner keeps its
      own latitude and longitude (the box always has 4 vertices and may be
      rotated); or
   b. drag the right mouse button over the map to select the whole area at
      once - an axis-aligned rectangle is created from the drag's two corners.
5. A JSON object is built and appended to the array that is written to
   map_objects.json.
6. Corner markers are only shown for one object at a time - the last one added,
   or the one currently selected - and can be dragged with the mouse; coordinates
   are updated in the file. All other objects still show their bbox outline.
7. On the right there is a list of objects: selecting a row highlights the
   object's box on the map and shows its corner markers, the "Delete selected"
   button / the Del key removes an object, a double click centers the map on the
   object. Clicking inside an object's bbox area on the map selects it in the
   list the same way.
8. The map can be moved with the arrow keys (and also dragged with the mouse /
   zoomed with the wheel).

Dependency:
    pip install tkintermapview
"""

import json
import math
import os
import tkinter as tk
from tkinter import simpledialog, messagebox

try:
    from tkintermapview import TkinterMapView
except ImportError:
    raise SystemExit("tkintermapview not found. Install it: pip install tkintermapview")


# ---------------------------------------------------------------------------
# Settings
# ---------------------------------------------------------------------------
OUTPUT_FILE = "map_objects.json"
OBJECT_TYPES = [
    "building",
    "road",
    "bridge",
    "industrial",
    "commercial",
    "water",
    "tree",
    "car",
    "other",
]
# Named bbox corners: x = longitude, y = latitude.
BBOX_KEYS = ["x_min", "y_min", "x_max", "y_max"]
# Windows virtual-key code of the physical "B" key - used instead of the "b" keysym so the
# building quick-pick hotkey works no matter which keyboard layout (e.g. Ukrainian) is active.
BUILDING_HOTKEY_VK = 0x42
START_ZOOM = 17
DRAG_HIT_RADIUS = 22  # px - how close to a marker you must click to grab it

BBOX_COLOR = "#ff3b30"            # normal bbox outline
BBOX_HIGHLIGHT_COLOR = "#00e5ff"  # highlighted outline (object selected in the list)
BBOX_WIDTH = 2
BBOX_HIGHLIGHT_WIDTH = 4

# Google Maps tiles. lyrs options:
#   m  - plain map (roads)
#   s  - satellite
#   y  - hybrid (satellite + labels)  <- handy for marking buildings/trees
#   p  - terrain
GOOGLE_TILE_SERVER = "https://mt0.google.com/vt/lyrs=y&hl=en&x={x}&y={y}&z={z}&s=Ga"


class MapApp:
    def __init__(self, root, start_lat, start_lon):
        self.root = root
        self.objects = []          # array of all objects (written to the file)
        self.next_id = 0           # elementId counter, starts at 0

        self.pending_type = None   # type picked in the dropdown, until points are placed
        self.pending_points = []   # accumulated points of the current object
        self.pending_markers = []  # temporary markers of the current (unfinished) object

        self.rect_start = None         # (lat, lon) where a right-button area drag started
        self.rect_start_canvas = None  # (x, y) canvas coords of that start point
        self.rect_select_id = None     # temporary canvas rectangle id while dragging

        self.polygons = {}         # element_id -> CanvasPolygon (bbox outline)
        self.corner_markers = []   # [{"element_id", "which", "marker"}] - only for the active object
        self.drag = None           # corner_markers entry currently being dragged
        self.selected_id = None    # elementId of the object selected in the list (highlighted)
        self.active_id = None      # elementId whose corner markers are currently shown

        self._load_existing()

        # --- window ---
        self.root.title("Map Object Marker")
        self.root.geometry("1200x800")

        # --- top bar: "+" button and status ---
        top = tk.Frame(root)
        top.pack(side="top", fill="x")

        self.add_btn = tk.Button(
            top, text="+", font=("Arial", 14, "bold"), width=3,
            command=self.show_type_dropdown,
        )
        self.add_btn.pack(side="left", padx=6, pady=6)

        self.status = tk.Label(
            top, text="Ready. Press \"+\" to add an object (or \"b\" for building).", anchor="w"
        )
        self.status.pack(side="left", padx=10)

        # --- dropdown with object types ---
        self.type_menu = tk.Menu(root, tearoff=0)
        for obj_type in OBJECT_TYPES:
            self.type_menu.add_command(
                label=obj_type,
                command=lambda t=obj_type: self.start_picking(t),
            )

        # --- right panel: object list ---
        side = tk.Frame(root, width=250)
        side.pack(side="right", fill="y")
        side.pack_propagate(False)
        tk.Label(side, text="Objects", font=("Arial", 11, "bold")).pack(pady=(8, 2))
        self.listbox = tk.Listbox(side, activestyle="dotbox", exportselection=False)
        self.listbox.pack(fill="both", expand=True, padx=6)
        self.listbox.bind("<<ListboxSelect>>", self._on_list_select)
        self.listbox.bind("<Double-Button-1>", self._on_list_double_click)
        self.listbox.bind("<Delete>", lambda e: self.delete_selected())
        tk.Button(side, text="Delete selected", command=self.delete_selected).pack(
            fill="x", padx=6, pady=6
        )
        tk.Label(
            side,
            text="- drag a marker to move\n  a point\n- right-drag on map to add\n  a whole area at once\n- Del / button - delete\n- click - highlight on the map\n- double click - jump to object\n- click object area on map\n  to select it here",
            font=("Arial", 8), fg="#555", justify="left",
        ).pack(padx=6, pady=(0, 8), anchor="w")

        # --- map ---
        self.map_widget = TkinterMapView(root, corner_radius=0)
        self.map_widget.pack(fill="both", expand=True)
        self.map_widget.set_tile_server(GOOGLE_TILE_SERVER, max_zoom=22)
        self.map_widget.set_position(start_lat, start_lon)
        self.map_widget.set_zoom(START_ZOOM)
        self.map_widget.add_left_click_map_command(self.on_map_click)

        # --- intercept the mouse to drag corner markers ---
        canvas = self.map_widget.canvas
        self._orig_click = self.map_widget.mouse_click
        self._orig_move = self.map_widget.mouse_move
        self._orig_release = self.map_widget.mouse_release
        canvas.bind("<Button-1>", self._on_canvas_press)
        canvas.bind("<B1-Motion>", self._on_canvas_drag)
        canvas.bind("<ButtonRelease-1>", self._on_canvas_release)

        # --- right mouse button: drag a rectangle to select the whole area at once ---
        canvas.bind("<Button-3>", self._on_canvas_right_press)
        canvas.bind("<B3-Motion>", self._on_canvas_right_drag)
        canvas.bind("<ButtonRelease-3>", self._on_canvas_right_release)

        self._draw_existing()
        self._refresh_list()

        # --- move the map with the arrow keys ---
        self.root.bind_all("<Left>", lambda e: self.pan(0, -1))
        self.root.bind_all("<Right>", lambda e: self.pan(0, 1))
        self.root.bind_all("<Up>", lambda e: self.pan(1, 0))
        self.root.bind_all("<Down>", lambda e: self.pan(-1, 0))
        # --- cancel the current pick ---
        self.root.bind_all("<Escape>", lambda e: self.cancel_picking())
        # --- quick-pick "building" type (by physical key, not layout-dependent keysym) ---
        self.root.bind_all("<KeyPress>", self._on_global_keypress)

    # ------------------------------------------------------------------ file
    def _load_existing(self):
        """Loads the existing map_objects.json to continue the numbering."""
        if not os.path.exists(OUTPUT_FILE):
            return
        try:
            with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (json.JSONDecodeError, OSError):
            return
        if not isinstance(data, list):
            return

        self.objects = data
        max_id = -1
        for obj in data:
            eid = str(obj.get("elementId", ""))
            if eid.startswith("obj"):
                try:
                    max_id = max(max_id, int(eid[3:]))
                except ValueError:
                    pass
        self.next_id = max_id + 1

    def _save(self):
        with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
            json.dump(self.objects, f, indent=4, ensure_ascii=False)

    def _get_object(self, element_id):
        for obj in self.objects:
            if obj.get("elementId") == element_id:
                return obj
        return None

    # ------------------------------------------------------------- drawing
    def _draw_existing(self):
        for obj in self.objects:
            try:
                self._redraw_bbox(obj)
            except (KeyError, TypeError):
                pass
        if self.objects:
            self._set_active_markers(self.objects[-1].get("elementId"))

    def _set_active_markers(self, element_id):
        """Corner markers are only ever shown for one object: the last one
        added, or the one currently selected in the list / clicked on the map."""
        if element_id == self.active_id:
            return
        self._clear_active_markers()
        self.active_id = element_id
        if element_id is None:
            return
        obj = self._get_object(element_id)
        if obj is None:
            return
        try:
            bbox = obj["bbox"]
            for key in BBOX_KEYS:
                p = bbox[key]
                self._place_corner_marker(
                    p["latitude"], p["longitude"], element_id, key, obj.get("type", "")
                )
        except (KeyError, TypeError):
            pass

    def _clear_active_markers(self):
        if self.active_id is None:
            return
        kept = []
        for entry in self.corner_markers:
            if entry["element_id"] == self.active_id:
                try:
                    entry["marker"].delete()
                except Exception:
                    pass
            else:
                kept.append(entry)
        self.corner_markers = kept

    def _place_corner_marker(self, lat, lon, element_id, which, obj_type):
        marker = self.map_widget.set_marker(
            lat, lon,
            text=f"{obj_type}:{which}",
            marker_color_circle="#8c1d16",
            marker_color_outside="#ff3b30",
        )
        self.corner_markers.append({"element_id": element_id, "which": which, "marker": marker})
        return marker

    def _bbox_corners_ordered(self, obj):
        """The 4 bbox corners in walk order around the circle (by angle around the centroid)."""
        bbox = obj["bbox"]
        pts = [(bbox[k]["latitude"], bbox[k]["longitude"]) for k in BBOX_KEYS]
        clat = sum(p[0] for p in pts) / len(pts)
        clon = sum(p[1] for p in pts) / len(pts)
        pts.sort(key=lambda p: math.atan2(p[0] - clat, p[1] - clon))
        return pts

    def _redraw_bbox(self, obj):
        eid = obj["elementId"]
        corners = self._bbox_corners_ordered(obj)
        poly = self.polygons.get(eid)
        if poly is not None:
            poly.position_list = corners
            poly.draw()
        else:
            self.polygons[eid] = self.map_widget.set_polygon(
                corners, name=eid, outline_color=BBOX_COLOR, fill_color=None,
                border_width=BBOX_WIDTH,
            )
        self._apply_highlight()

    def _apply_highlight(self):
        """The box selected in the list is bright and thicker; the rest are normal."""
        canvas = self.map_widget.canvas
        for eid, poly in self.polygons.items():
            on = (eid == self.selected_id)
            color = BBOX_HIGHLIGHT_COLOR if on else BBOX_COLOR
            width = BBOX_HIGHLIGHT_WIDTH if on else BBOX_WIDTH
            poly.outline_color = color
            poly.border_width = width
            cp = getattr(poly, "canvas_polygon", None)
            if cp is not None:
                try:
                    canvas.itemconfig(cp, outline=color, width=width)
                    if on:
                        canvas.tag_raise(cp)
                except Exception:
                    pass

    def _remove_object_visuals(self, element_id):
        poly = self.polygons.pop(element_id, None)
        if poly is not None:
            try:
                poly.delete()
            except Exception:
                pass
        kept = []
        for entry in self.corner_markers:
            if entry["element_id"] == element_id:
                try:
                    entry["marker"].delete()
                except Exception:
                    pass
            else:
                kept.append(entry)
        self.corner_markers = kept
        if self.drag is not None and self.drag["element_id"] == element_id:
            self.drag = None
        if self.active_id == element_id:
            self.active_id = None

    def _clear_pending_markers(self):
        for m in self.pending_markers:
            try:
                m.delete()
            except Exception:
                pass
        self.pending_markers = []

    # ------------------------------------------------------------- list UI
    def _refresh_list(self):
        self.listbox.delete(0, tk.END)
        for obj in self.objects:
            self.listbox.insert(tk.END, f"{obj.get('elementId', '?')}   -   {obj.get('type', '?')}")

    def _on_list_select(self, _event=None):
        sel = self.listbox.curselection()
        if not sel or sel[0] >= len(self.objects):
            return
        eid = self.objects[sel[0]].get("elementId")
        self.selected_id = eid
        self._set_active_markers(eid)
        self._apply_highlight()

    def _on_list_double_click(self, _event):
        sel = self.listbox.curselection()
        if not sel or sel[0] >= len(self.objects):
            return
        obj = self.objects[sel[0]]
        try:
            bbox = obj["bbox"]
            pts = [(bbox[k]["latitude"], bbox[k]["longitude"]) for k in BBOX_KEYS]
        except (KeyError, TypeError):
            return
        self.map_widget.set_position(
            sum(p[0] for p in pts) / len(pts),
            sum(p[1] for p in pts) / len(pts),
        )

    def _select_object_by_id(self, element_id):
        """Selects an object in the list (and shows its corner markers), e.g. after
        clicking its area on the map."""
        for i, obj in enumerate(self.objects):
            if obj.get("elementId") == element_id:
                self.listbox.selection_clear(0, tk.END)
                self.listbox.selection_set(i)
                self.listbox.see(i)
                self.status.config(text=f"Selected {element_id} ({obj.get('type', '?')}).")
                break
        self.selected_id = element_id
        self._set_active_markers(element_id)
        self._apply_highlight()

    @staticmethod
    def _point_in_polygon(point, polygon):
        """Ray-casting point-in-polygon test; point/polygon entries are (lat, lon)."""
        x, y = point[1], point[0]
        inside = False
        x1, y1 = polygon[-1][1], polygon[-1][0]
        for x2, y2 in ((p[1], p[0]) for p in polygon):
            if (y1 > y) != (y2 > y):
                x_at_y = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
                if x < x_at_y:
                    inside = not inside
            x1, y1 = x2, y2
        return inside

    def _select_object_at(self, coords):
        """Selects (in the list) the topmost object whose bbox area contains the click."""
        for obj in reversed(self.objects):
            try:
                corners = self._bbox_corners_ordered(obj)
            except (KeyError, TypeError):
                continue
            if self._point_in_polygon(coords, corners):
                self._select_object_by_id(obj.get("elementId"))
                return

    def delete_selected(self):
        sel = self.listbox.curselection()
        if not sel:
            self.status.config(text="Select an object in the list to delete it.")
            return
        index = sel[0]
        if index >= len(self.objects):
            return
        obj = self.objects[index]
        eid = obj.get("elementId", "?")
        if not messagebox.askyesno("Delete", f"Delete {eid} ({obj.get('type', '?')})?"):
            return
        self._remove_object_visuals(eid)
        self.objects.pop(index)
        if self.selected_id == eid:
            self.selected_id = None
        self._save()
        self._refresh_list()
        self._apply_highlight()
        self.status.config(text=f"Deleted {eid}. Total objects: {len(self.objects)}.")

    # ------------------------------------------------------------- dropdown
    def show_type_dropdown(self):
        x = self.add_btn.winfo_rootx()
        y = self.add_btn.winfo_rooty() + self.add_btn.winfo_height()
        self.type_menu.tk_popup(x, y)

    def start_picking(self, obj_type):
        self.pending_type = obj_type
        self.pending_points = []
        self._clear_pending_markers()
        self.status.config(
            text=f"Type: {obj_type}. Click 4 points on the map, "
                 f"or drag the right mouse button to select the whole area."
        )

    def cancel_picking(self):
        self._cancel_rect_select()
        if self.pending_type is None:
            return
        self._clear_pending_markers()
        self.pending_type = None
        self.pending_points = []
        self.status.config(text="Cancelled. Press \"+\" to add an object.")

    # ------------------------------------------------------------- clicks (new points)
    POINTS_PER_OBJECT = 4

    def on_map_click(self, coords):
        if self.pending_type is None:
            self._select_object_at(coords)
            return
        lat, lon = coords
        self.pending_points.append((lat, lon))
        idx = len(self.pending_points)

        marker = self.map_widget.set_marker(lat, lon, text=f"{self.pending_type}:P{idx}")
        self.pending_markers.append(marker)

        if idx < self.POINTS_PER_OBJECT:
            self.status.config(
                text=f"Type: {self.pending_type}. Click point {idx + 1} of {self.POINTS_PER_OBJECT}."
            )
        else:
            self._finalize_object()

    def _finalize_object(self):
        pts = self.pending_points[:self.POINTS_PER_OBJECT]
        # Each of the 4 clicks becomes a separate corner so the box always has 4 vertices.
        # First the two extreme points by longitude (x), then the remaining two by latitude (y).
        rest = list(pts)
        x_min_pt = min(rest, key=lambda p: p[1]); rest.remove(x_min_pt)
        x_max_pt = max(rest, key=lambda p: p[1]); rest.remove(x_max_pt)
        rest.sort(key=lambda p: p[0])
        y_min_pt, y_max_pt = rest[0], rest[1]
        corner_pts = {
            "x_min": x_min_pt,
            "y_min": y_min_pt,
            "x_max": x_max_pt,
            "y_max": y_max_pt,
        }
        self._commit_new_object(corner_pts)

    def _commit_new_object(self, corner_pts):
        """Creates and saves a new object from 4 (latitude, longitude) corner points,
        keyed by BBOX_KEYS. Shared by the point-by-point flow and the right-drag
        whole-area flow."""
        eid = f"obj{self.next_id}"

        obj = {
            "elementId": eid,
            "type": self.pending_type,
            "bbox": {
                key: {"latitude": p[0], "longitude": p[1]}
                for key, p in corner_pts.items()
            },
        }
        self.objects.append(obj)
        self.next_id += 1

        # remove the temporary markers, the new object becomes the active one
        # (its corner markers are shown and can be dragged)
        self._clear_pending_markers()
        self._redraw_bbox(obj)
        self._set_active_markers(eid)

        self._save()
        self._refresh_list()
        self.status.config(
            text=f"Created {eid} ({obj['type']}). Written to {OUTPUT_FILE}. Total: {len(self.objects)}."
        )
        self.pending_type = None
        self.pending_points = []

    # ------------------------------------------------------- right-drag whole-area select
    def _cancel_rect_select(self):
        if self.rect_select_id is not None:
            self.map_widget.canvas.delete(self.rect_select_id)
        self.rect_select_id = None
        self.rect_start = None
        self.rect_start_canvas = None

    def _on_canvas_right_press(self, event):
        if self.pending_type is None:
            self.status.config(
                text="Pick a type with \"+\" first, then drag the right mouse "
                     "button to select the whole area."
            )
            return "break"
        # switching to the rectangle flow abandons any in-progress point clicks
        self._clear_pending_markers()
        self.pending_points = []

        lat, lon = self.map_widget.convert_canvas_coords_to_decimal_coords(event.x, event.y)
        self.rect_start = (lat, lon)
        self.rect_start_canvas = (event.x, event.y)
        self.rect_select_id = self.map_widget.canvas.create_rectangle(
            event.x, event.y, event.x, event.y,
            outline=BBOX_COLOR, width=2, dash=(4, 2),
        )
        self.status.config(text=f"Type: {self.pending_type}. Drag to select the area...")
        return "break"

    def _on_canvas_right_drag(self, event):
        if self.rect_select_id is None:
            return "break"
        x0, y0 = self.rect_start_canvas
        self.map_widget.canvas.coords(self.rect_select_id, x0, y0, event.x, event.y)
        return "break"

    def _on_canvas_right_release(self, event):
        if self.rect_select_id is None:
            return "break"
        start_lat, start_lon = self.rect_start
        self._cancel_rect_select()
        end_lat, end_lon = self.map_widget.convert_canvas_coords_to_decimal_coords(event.x, event.y)

        if abs(end_lat - start_lat) < 1e-9 and abs(end_lon - start_lon) < 1e-9:
            self.status.config(text="Area too small - drag the right mouse button to select a rectangle.")
            return "break"

        lat_min, lat_max = sorted((start_lat, end_lat))
        lon_min, lon_max = sorted((start_lon, end_lon))
        corner_pts = {
            "x_min": (lat_min, lon_min),
            "y_min": (lat_min, lon_max),
            "x_max": (lat_max, lon_max),
            "y_max": (lat_max, lon_min),
        }
        self._commit_new_object(corner_pts)
        return "break"

    # ------------------------------------------------------- dragging markers
    def _find_corner_at(self, x, y):
        best_entry, best_dist = None, DRAG_HIT_RADIUS
        for entry in self.corner_markers:
            marker = entry["marker"]
            try:
                cx, cy = marker.get_canvas_pos(marker.position)
            except Exception:
                continue
            dist = math.hypot(x - cx, y - cy)
            if dist <= best_dist:
                best_entry, best_dist = entry, dist
        return best_entry

    def _on_canvas_press(self, event):
        entry = self._find_corner_at(event.x, event.y)
        if entry is not None:
            self.drag = entry
            self.status.config(text=f"Moving {entry['element_id']} / {entry['which']} ...")
            return "break"
        return self._orig_click(event)

    def _on_canvas_drag(self, event):
        if self.drag is None:
            return self._orig_move(event)
        lat, lon = self.map_widget.convert_canvas_coords_to_decimal_coords(event.x, event.y)
        self.drag["marker"].set_position(lat, lon)
        obj = self._get_object(self.drag["element_id"])
        if obj is not None:
            point = obj["bbox"][self.drag["which"]]
            point["latitude"], point["longitude"] = lat, lon
            self._redraw_bbox(obj)
        return "break"

    def _on_canvas_release(self, event):
        if self.drag is None:
            return self._orig_release(event)
        entry, self.drag = self.drag, None
        self._save()
        obj = self._get_object(entry["element_id"])
        if obj is not None:
            point = obj["bbox"][entry["which"]]
            self.status.config(
                text=f"{entry['element_id']} / {entry['which']} -> "
                     f"{point['latitude']:.6f}, {point['longitude']:.6f}. Saved."
            )
        return "break"

    # ------------------------------------------------------------- moving the map
    def pan(self, lat_dir, lon_dir):
        lat, lon = self.map_widget.get_position()
        zoom = getattr(self.map_widget, "zoom", START_ZOOM)
        step = 360.0 / (2 ** zoom) * 0.5
        self.map_widget.set_position(lat + lat_dir * step, lon + lon_dir * step)

    # ------------------------------------------------------------- global hotkeys
    def _on_global_keypress(self, event):
        # event.keycode is the physical key's virtual-key code, unaffected by the
        # active keyboard layout (unlike event.keysym / event.char).
        if event.keycode == BUILDING_HOTKEY_VK:
            self.start_picking("building")


def first_object_coords():
    """The bbox center of the first object in map_objects.json, or None if there are no objects."""
    if not os.path.exists(OUTPUT_FILE):
        return None
    try:
        with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (json.JSONDecodeError, OSError):
        return None
    if not isinstance(data, list) or not data:
        return None
    try:
        bbox = data[0]["bbox"]
        pts = [(bbox[k]["latitude"], bbox[k]["longitude"]) for k in BBOX_KEYS]
    except (KeyError, TypeError):
        return None
    return (
        sum(p[0] for p in pts) / len(pts),
        sum(p[1] for p in pts) / len(pts),
    )


def ask_coordinates(root):
    lat = simpledialog.askfloat(
        "Coordinates", "Enter latitude:", parent=root,
        minvalue=-90.0, maxvalue=90.0,
    )
    if lat is None:
        return None
    lon = simpledialog.askfloat(
        "Coordinates", "Enter longitude:", parent=root,
        minvalue=-180.0, maxvalue=180.0,
    )
    if lon is None:
        return None
    return lat, lon


def main():
    root = tk.Tk()
    root.withdraw()

    coords = first_object_coords()
    if coords is None:
        coords = ask_coordinates(root)
    if coords is None:
        messagebox.showinfo("Exit", "No coordinates entered. Quitting.")
        root.destroy()
        return

    root.deiconify()
    MapApp(root, coords[0], coords[1])
    root.mainloop()


if __name__ == "__main__":
    main()
