"""
Creates Unreal Engine DataTable assets from SU2 extrapolated polar JSON.
Must run inside the Unreal Editor Python environment (imports unreal).

Asset placement:
  - Path : /Game/<Content-relative folder of the .dat file>/
            e.g. /Game/WingProfile/NACA_0009/
  - Name : DT_<airfoil_stem>_<hinge>_<min_flap>_<max_flap>
            e.g. DT_naca0009_0-75_-40-0_40-0
            (dots in floats replaced with dashes — Unreal rejects dots in asset names)
"""

import json
from pathlib import Path

try:
    import unreal
    _UNREAL_AVAILABLE = True
except ModuleNotFoundError:
    unreal = None
    _UNREAL_AVAILABLE = False

PROJECT_NAME = "UAVSimulator"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _asset_folder_from_profile(profile_path: str) -> str:
    """Return the /Game/... folder derived from the .dat file path."""
    parts = Path(profile_path).parts
    try:
        idx = next(i for i, p in enumerate(parts) if p == "Content")
    except StopIteration:
        raise ValueError(f"'Content' not found in profile path: {profile_path}")
    relative = "/".join(parts[idx + 1:-1])   # strip 'Content' prefix and filename
    return f"/Game/{relative}/"


def _asset_name(stem: str, hinge: float, min_flap: float, max_flap: float) -> str:
    def _fmt(v: float) -> str:
        return str(v).replace(".", "-")
    return f"DT_{stem}_{_fmt(hinge)}_{_fmt(min_flap)}_{_fmt(max_flap)}"


def _create_curve_data(keys_data: list) -> dict:
    """Convert [{Time, Value}, ...] to full FRuntimeFloatCurve structure."""
    full_keys = []
    for key in keys_data:
        full_keys.append({
            "InterpMode":         "RCIM_Cubic",
            "TangentMode":        "RCTM_Auto",
            "TangentWeightMode":  "RCTWM_WeightedNone",
            "Time":               float(key["Time"]),
            "Value":              float(key["Value"]),
            "ArriveTangent":      0.0,
            "ArriveTangentWeight": 0.0,
            "LeaveTangent":       0.0,
            "LeaveTangentWeight": 0.0,
            "KeyHandlesToIndices": "",
        })
    return {
        "EditorCurveData": {
            "KeyHandlesToIndices": {},
            "Keys":                full_keys,
            "DefaultValue":        3.402823466e+38,
            "PreInfinityExtrap":   "RCCE_Constant",
            "PostInfinityExtrap":  "RCCE_Constant",
        },
        "ExternalCurve": "",
    }


def _create_datatable(rows_data: list, asset_folder: str, asset_name: str) -> None:
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    struct_path = f"/Script/{PROJECT_NAME}.AerodynamicProfileRow"
    row_struct = unreal.load_object(None, struct_path)
    if not row_struct:
        unreal.log_error(f"Could not find struct: {struct_path}")
        return

    full_path = asset_folder + asset_name
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        unreal.log(f"Asset '{full_path}' exists — deleting for overwrite.")
        if not unreal.EditorAssetLibrary.delete_asset(full_path):
            unreal.log_error(f"Failed to delete existing asset: {full_path}")
            return

    factory = unreal.DataTableFactory()
    factory.struct = row_struct

    try:
        data_table = asset_tools.create_asset(asset_name, asset_folder, unreal.DataTable, factory)
        if not data_table:
            raise RuntimeError("create_asset returned None")
        unreal.log(f"Created asset: {data_table.get_path_name()}")
    except Exception as e:
        unreal.log_error(f"Failed to create asset: {e}")
        return

    json_string = json.dumps(rows_data, indent=4)
    try:
        ok = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(data_table, json_string)
        if not ok:
            unreal.log_error(f"fill_data_table_from_json_string failed for {asset_name}")
            return
        unreal.EditorAssetLibrary.save_loaded_asset(data_table)
        unreal.log(f"Saved asset: {full_path}")
    except Exception as e:
        unreal.log_error(f"Error filling/saving asset: {e}")


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def create_unreal_assets(
    extrapolated_json_path: str,
    profile_path:   str,
    hinge_location: float,
    min_flap:       float,
    max_flap:       float,
) -> None:
    """
    Read the extrapolated polar JSON produced by extrapolate_and_save() and
    create a single DataTable asset in Unreal Engine.
    """
    if not _UNREAL_AVAILABLE:
        print(
            "[su2_unreal_import] 'unreal' module not found — skipping DataTable creation.\n"
            f"  JSON result saved at: {extrapolated_json_path}\n"
            "  To import into Unreal, run this script from within the Unreal Editor Python environment."
        )
        return

    with open(extrapolated_json_path, encoding="utf-8") as f:
        raw_results = json.load(f)

    rows_data = []
    for entry in raw_results:
        rows_data.append({
            "Name":      entry["Name"],
            "FlapAngle": entry["FlapAngle"],
            "ClVsAoA":   _create_curve_data(entry["ClVsAoA"]),
            "CdVsAoA":   _create_curve_data(entry["CdVsAoA"]),
            "CmVsAoA":   _create_curve_data(entry["CmVsAoA"]),
        })

    stem         = Path(profile_path).stem
    asset_folder = _asset_folder_from_profile(profile_path)
    name         = _asset_name(stem, hinge_location, min_flap, max_flap)

    unreal.log(
        f"Creating DataTable '{name}' at '{asset_folder}' "
        f"({len(rows_data)} flap angles)"
    )
    _create_datatable(rows_data, asset_folder, name)
