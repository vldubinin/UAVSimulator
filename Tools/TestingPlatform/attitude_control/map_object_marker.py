"""
map_object_marker.py

Інтерактивна карта Google Maps для розмітки об'єктів (building / tree / other).

Що робить скрипт:
1. При старті питає latitude та longitude.
2. Відкриває вікно з картою Google у вказаних координатах.
3. Кнопка "+" -> дропдаун зі списком типів об'єктів (building, tree, other).
4. Після вибору типу треба клікнути мишкою 4 точки (кути) на карті; з них
   обчислюється охоплююча рамка bbox: pointTop = (min lat, min lon),
   pointBottom = (max lat, max lon).
5. Формується JSON-об'єкт і додається в масив, який пишеться у файл map_objects.json.
6. Поставлені кутові маркери можна перетягувати мишкою — координати оновлюються у файлі.
7. Праворуч є список об'єктів: кнопка "Видалити обране" / клавіша Del видаляє об'єкт,
   подвійний клік центрує карту на об'єкті.
8. Карту можна рухати стрілочками (а також перетягуванням мишкою / колесом зумити).

Залежність:
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
    raise SystemExit("Не знайдено tkintermapview. Встановіть: pip install tkintermapview")


# ---------------------------------------------------------------------------
# Налаштування
# ---------------------------------------------------------------------------
OUTPUT_FILE = "map_objects.json"
OBJECT_TYPES = ["building", "tree", "other"]
ALTITUDE = 350
START_ZOOM = 17
DRAG_HIT_RADIUS = 22  # px — наскільки близько до маркера треба клікнути, щоб схопити його

# Тайли Google Maps. Варіанти lyrs:
#   m  - звичайна карта (дороги)
#   s  - супутник
#   y  - гібрид (супутник + підписи)  <- зручно для розмітки будівель/дерев
#   p  - рельєф
GOOGLE_TILE_SERVER = "https://mt0.google.com/vt/lyrs=y&hl=uk&x={x}&y={y}&z={z}&s=Ga"


class MapApp:
    def __init__(self, root, start_lat, start_lon):
        self.root = root
        self.objects = []          # масив усіх об'єктів (пишеться у файл)
        self.next_id = 0           # лічильник elementId, починаємо з 0

        self.pending_type = None   # обраний у дропдауні тип, поки не поставлено 2 точки
        self.pending_points = []   # накопичені точки поточного об'єкта
        self.pending_markers = []  # тимчасові маркери поточного (незавершеного) об'єкта

        self.polygons = {}         # element_id -> CanvasPolygon (рамка bbox)
        self.corner_markers = []   # [{"element_id", "which", "marker"}]
        self.drag = None           # активний запис corner_markers, що зараз тягнемо

        self._load_existing()

        # --- вікно ---
        self.root.title("Map Object Marker")
        self.root.geometry("1200x800")

        # --- верхня панель: кнопка "+" та статус ---
        top = tk.Frame(root)
        top.pack(side="top", fill="x")

        self.add_btn = tk.Button(
            top, text="+", font=("Arial", 14, "bold"), width=3,
            command=self.show_type_dropdown,
        )
        self.add_btn.pack(side="left", padx=6, pady=6)

        self.status = tk.Label(top, text="Готово. Натисніть \"+\", щоб додати об'єкт.", anchor="w")
        self.status.pack(side="left", padx=10)

        # --- дропдаун з типами об'єктів ---
        self.type_menu = tk.Menu(root, tearoff=0)
        for obj_type in OBJECT_TYPES:
            self.type_menu.add_command(
                label=obj_type,
                command=lambda t=obj_type: self.start_picking(t),
            )

        # --- права панель: список об'єктів ---
        side = tk.Frame(root, width=250)
        side.pack(side="right", fill="y")
        side.pack_propagate(False)
        tk.Label(side, text="Об'єкти", font=("Arial", 11, "bold")).pack(pady=(8, 2))
        self.listbox = tk.Listbox(side, activestyle="dotbox", exportselection=False)
        self.listbox.pack(fill="both", expand=True, padx=6)
        self.listbox.bind("<Double-Button-1>", self._on_list_double_click)
        self.listbox.bind("<Delete>", lambda e: self.delete_selected())
        tk.Button(side, text="Видалити обране", command=self.delete_selected).pack(
            fill="x", padx=6, pady=6
        )
        tk.Label(
            side,
            text="• перетягніть маркер, щоб\n  змінити точку\n• Del / кнопка — видалити\n• подвійний клік — до об'єкта",
            font=("Arial", 8), fg="#555", justify="left",
        ).pack(padx=6, pady=(0, 8), anchor="w")

        # --- карта ---
        self.map_widget = TkinterMapView(root, corner_radius=0)
        self.map_widget.pack(fill="both", expand=True)
        self.map_widget.set_tile_server(GOOGLE_TILE_SERVER, max_zoom=22)
        self.map_widget.set_position(start_lat, start_lon)
        self.map_widget.set_zoom(START_ZOOM)
        self.map_widget.add_left_click_map_command(self.on_map_click)

        # --- перехоплення мишки для перетягування кутових маркерів ---
        canvas = self.map_widget.canvas
        self._orig_click = self.map_widget.mouse_click
        self._orig_move = self.map_widget.mouse_move
        self._orig_release = self.map_widget.mouse_release
        canvas.bind("<Button-1>", self._on_canvas_press)
        canvas.bind("<B1-Motion>", self._on_canvas_drag)
        canvas.bind("<ButtonRelease-1>", self._on_canvas_release)

        self._draw_existing()
        self._refresh_list()

        # --- рух картою стрілочками ---
        self.root.bind_all("<Left>", lambda e: self.pan(0, -1))
        self.root.bind_all("<Right>", lambda e: self.pan(0, 1))
        self.root.bind_all("<Up>", lambda e: self.pan(1, 0))
        self.root.bind_all("<Down>", lambda e: self.pan(-1, 0))
        # --- скасувати поточний вибір ---
        self.root.bind_all("<Escape>", lambda e: self.cancel_picking())

    # ------------------------------------------------------------------ файл
    def _load_existing(self):
        """Підвантажує вже існуючий map_objects.json, щоб продовжити нумерацію."""
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

    # ------------------------------------------------------------- малювання
    def _draw_existing(self):
        for obj in self.objects:
            try:
                pt = obj["bbox"]["pointTop"]
                pb = obj["bbox"]["pointBottom"]
                eid = obj["elementId"]
                self._place_corner_marker(pt["latitude"], pt["longitude"], eid, "pointTop", obj.get("type", ""))
                self._place_corner_marker(pb["latitude"], pb["longitude"], eid, "pointBottom", obj.get("type", ""))
                self._redraw_bbox(obj)
            except (KeyError, TypeError):
                pass

    def _place_corner_marker(self, lat, lon, element_id, which, obj_type):
        marker = self.map_widget.set_marker(
            lat, lon,
            text=f"{obj_type}:{which}",
            marker_color_circle="#8c1d16",
            marker_color_outside="#ff3b30",
        )
        self.corner_markers.append({"element_id": element_id, "which": which, "marker": marker})
        return marker

    def _redraw_bbox(self, obj):
        eid = obj["elementId"]
        pt = obj["bbox"]["pointTop"]
        pb = obj["bbox"]["pointBottom"]
        corners = [
            (pt["latitude"], pt["longitude"]),
            (pt["latitude"], pb["longitude"]),
            (pb["latitude"], pb["longitude"]),
            (pb["latitude"], pt["longitude"]),
        ]
        poly = self.polygons.get(eid)
        if poly is not None:
            poly.position_list = corners
            poly.draw()
        else:
            self.polygons[eid] = self.map_widget.set_polygon(
                corners, name=eid, outline_color="#ff3b30", fill_color=None, border_width=2
            )

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

    def _clear_pending_markers(self):
        for m in self.pending_markers:
            try:
                m.delete()
            except Exception:
                pass
        self.pending_markers = []

    # ------------------------------------------------------------- список UI
    def _refresh_list(self):
        self.listbox.delete(0, tk.END)
        for obj in self.objects:
            self.listbox.insert(tk.END, f"{obj.get('elementId', '?')}   —   {obj.get('type', '?')}")

    def _on_list_double_click(self, _event):
        sel = self.listbox.curselection()
        if not sel or sel[0] >= len(self.objects):
            return
        obj = self.objects[sel[0]]
        try:
            pt = obj["bbox"]["pointTop"]
            pb = obj["bbox"]["pointBottom"]
        except (KeyError, TypeError):
            return
        self.map_widget.set_position(
            (pt["latitude"] + pb["latitude"]) / 2.0,
            (pt["longitude"] + pb["longitude"]) / 2.0,
        )

    def delete_selected(self):
        sel = self.listbox.curselection()
        if not sel:
            self.status.config(text="Оберіть об'єкт у списку, щоб видалити.")
            return
        index = sel[0]
        if index >= len(self.objects):
            return
        obj = self.objects[index]
        eid = obj.get("elementId", "?")
        if not messagebox.askyesno("Видалити", f"Видалити {eid} ({obj.get('type', '?')})?"):
            return
        self._remove_object_visuals(eid)
        self.objects.pop(index)
        self._save()
        self._refresh_list()
        self.status.config(text=f"Видалено {eid}. Всього об'єктів: {len(self.objects)}.")

    # ------------------------------------------------------------- дропдаун
    def show_type_dropdown(self):
        x = self.add_btn.winfo_rootx()
        y = self.add_btn.winfo_rooty() + self.add_btn.winfo_height()
        self.type_menu.tk_popup(x, y)

    def start_picking(self, obj_type):
        self.pending_type = obj_type
        self.pending_points = []
        self._clear_pending_markers()
        self.status.config(text=f"Тип: {obj_type}. Клікніть точку 1 з 4 на карті.")

    def cancel_picking(self):
        if self.pending_type is None:
            return
        self._clear_pending_markers()
        self.pending_type = None
        self.pending_points = []
        self.status.config(text="Скасовано. Натисніть \"+\", щоб додати об'єкт.")

    # ------------------------------------------------------------- кліки (нові точки)
    POINTS_PER_OBJECT = 4

    def on_map_click(self, coords):
        if self.pending_type is None:
            return
        lat, lon = coords
        self.pending_points.append((lat, lon))
        idx = len(self.pending_points)

        marker = self.map_widget.set_marker(lat, lon, text=f"{self.pending_type}:P{idx}")
        self.pending_markers.append(marker)

        if idx < self.POINTS_PER_OBJECT:
            self.status.config(
                text=f"Тип: {self.pending_type}. Клікніть точку {idx + 1} з {self.POINTS_PER_OBJECT}."
            )
        else:
            self._finalize_object()

    def _finalize_object(self):
        pts = self.pending_points[:self.POINTS_PER_OBJECT]
        lats = [p[0] for p in pts]
        lons = [p[1] for p in pts]
        min_lat, max_lat = min(lats), max(lats)
        min_lon, max_lon = min(lons), max(lons)
        eid = f"obj{self.next_id}"

        obj = {
            "elementId": eid,
            "type": self.pending_type,
            "bbox": {
                "pointTop": {"latitude": min_lat, "longitude": min_lon},
                "pointBottom": {"latitude": max_lat, "longitude": max_lon},
            },
            "altitude": ALTITUDE,
        }
        self.objects.append(obj)
        self.next_id += 1

        # тимчасові маркери прибираємо, ставимо стилізовані кутові маркери (їх можна тягати)
        self._clear_pending_markers()
        self._place_corner_marker(min_lat, min_lon, eid, "pointTop", obj["type"])
        self._place_corner_marker(max_lat, max_lon, eid, "pointBottom", obj["type"])
        self._redraw_bbox(obj)

        self._save()
        self._refresh_list()
        self.status.config(
            text=f"Створено {eid} ({obj['type']}). Записано у {OUTPUT_FILE}. Всього: {len(self.objects)}."
        )
        self.pending_type = None
        self.pending_points = []

    # ------------------------------------------------------- перетягування маркерів
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
            self.status.config(text=f"Переміщення {entry['element_id']} / {entry['which']} …")
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
                text=f"{entry['element_id']} / {entry['which']} → "
                     f"{point['latitude']:.6f}, {point['longitude']:.6f}. Збережено."
            )
        return "break"

    # ------------------------------------------------------------- рух картою
    def pan(self, lat_dir, lon_dir):
        lat, lon = self.map_widget.get_position()
        zoom = getattr(self.map_widget, "zoom", START_ZOOM)
        step = 360.0 / (2 ** zoom) * 0.5
        self.map_widget.set_position(lat + lat_dir * step, lon + lon_dir * step)


def ask_coordinates(root):
    lat = simpledialog.askfloat(
        "Координати", "Введіть latitude (широта):", parent=root,
        minvalue=-90.0, maxvalue=90.0,
    )
    if lat is None:
        return None
    lon = simpledialog.askfloat(
        "Координати", "Введіть longitude (довгота):", parent=root,
        minvalue=-180.0, maxvalue=180.0,
    )
    if lon is None:
        return None
    return lat, lon


def main():
    root = tk.Tk()
    root.withdraw()

    coords = ask_coordinates(root)
    if coords is None:
        messagebox.showinfo("Вихід", "Координати не введено. Завершення.")
        root.destroy()
        return

    root.deiconify()
    MapApp(root, coords[0], coords[1])
    root.mainloop()


if __name__ == "__main__":
    main()
