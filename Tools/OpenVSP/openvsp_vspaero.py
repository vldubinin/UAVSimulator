import json
import sys

import openvsp as vsp
import unreal
from airfoilprep import Airfoil
from airfoilprep import Polar

# --- КОНФІГУРАЦІЯ ---
# Обов'язково замініть "MyProject" на назву вашого проекту.
# Назва проекту - це назва файлу .uproject (без розширення).
PROJECT_NAME = "UAVSimulator"

results_geo_path = r"C:\Users\PC\Documents\Unreal Projects\UAVSimulator\Tools\OpenVSP\result\sub_surface"
output_file = r"C:\Users\PC\Documents\Unreal Projects\UAVSimulator\Tools\OpenVSP\result\output.json"

alpha_start_val = -8  # початковий AoA
alpha_end_val = 10  # кінцевий AoA
alpha_npts_val = 19.0  # кількість значення AoA в полярі.
deflection_angle_step_val = 1  # крок flap angle
wake_num_iter_val = 3  # точність, мінімум 3

re = 750000.0
cd_max = 1.25

root_chord = int(sys.argv[1].strip())
tip_chord = int(sys.argv[2].strip())
span = int(sys.argv[3].strip())
deflection_angle_start_val = int(sys.argv[4].strip()) # початковий flap angle
deflection_angle_end_val = int(sys.argv[5].strip()) # кінцевий flap angle
sweep = int(sys.argv[6].strip())
flap = (deflection_angle_start_val != 0) & (deflection_angle_end_val != 0)
surface_name = sys.argv[7]
sub_surface_index = sys.argv[8]
airfoil_path = sys.argv[9]

#root_chord = 10
#tip_chord = 10
#span = 10
#deflection_angle_start_val = -45 # початковий flap angle
#deflection_angle_end_val = 45
#sweep = 0
#flap = True
#surface_name = "surface"
#sub_surface_index = 1
#airfoil_path = r"C:\Users\PC\Documents\Unreal Projects\UAVSimulator\Content\WingProfile\NACA_2412\naca2412.dat"
print(airfoil_path)

print(f"############# Початок розрахунку для {surface_name}. Sub surface: {sub_surface_index} #############")
ASSET_PATH = f"/Game/DataTables/{surface_name}/"
ASSET_NAME = f"DT_AerodynamicProfile_{sub_surface_index}"

print(flap)
surface_form = [{"span": span, "root_chord": root_chord, "tip_chord": tip_chord, "sweep": sweep, "flap": flap}]

print(surface_form)

vsp.VSPCheckSetup()

def calculate(alpha, deflection_angle, wake_num_iter, surface_geometry):
    vsp.ClearVSPModel()
    print("Створення геометрії")
    wing_id = vsp.AddGeom('WING', '')
    vsp.SetParmVal(vsp.FindParm(wing_id, "Sym_Planar_Flag", "Sym"), 0.0)

    vsp.Update()

    xsec_surf_id = vsp.GetXSecSurf(wing_id, 0)

    vsp.ChangeXSecShape(xsec_surf_id, 0, vsp.XS_FILE_AIRFOIL)
    root_xsec_id = vsp.GetXSec(xsec_surf_id, 0)
    res = vsp.ReadFileAirfoil(root_xsec_id, airfoil_path)

    tip_xsec_id = vsp.GetXSec(xsec_surf_id, 1)
    vsp.ChangeXSecShape(xsec_surf_id, 1, vsp.XS_FILE_AIRFOIL)
    vsp.ReadFileAirfoil(tip_xsec_id, airfoil_path)

    vsp.Update()

    # Split geo
    for sectionIndex in range(1, len(surface_geometry)):
        vsp.SplitWingXSec(wing_id, sectionIndex)
    vsp.Update()

    if (surface_geometry[0]["flap"]):
        vsp.AddSubSurf(wing_id, vsp.SS_CONTROL)
        vsp.AutoGroupVSPAEROControlSurfaces()
        vsp.Update()

    for i, geometry in enumerate(surface_geometry):
        vsp.SetParmVal(wing_id, "Span", f"XSec_{i + 1}", geometry["span"])
        vsp.SetParmVal(wing_id, "Root_Chord", f"XSec_{i + 1}", geometry["root_chord"])
        vsp.SetParmVal(wing_id, "Tip_Chord", f"XSec_{i + 1}", geometry["tip_chord"])
        vsp.SetParmVal(wing_id, 'Sweep', f"XSec_{i + 1}", geometry["sweep"])
        if geometry["flap"]:
            flop_offset = 1 / (len(surface_geometry) + 2)
            vsp.SetParmVal(wing_id, 'UStart', 'SS_Control_1', flop_offset * (i + 1))
            vsp.SetParmVal(wing_id, 'UEnd', 'SS_Control_1', flop_offset * (i + 2))

    vsp.Update()

    print("Задання кутів атаки та положення закрилків")
    control_group_settings_container_id = vsp.FindContainer("VSPAEROSettings", 0)

    if (surface_geometry[0]["flap"]):
        deflection_angle_id = vsp.FindParm(control_group_settings_container_id, "DeflectionAngle",
                                           "ControlSurfaceGroup_0")
        vsp.SetParmValUpdate(deflection_angle_id, deflection_angle)

    alpha_npts_id = vsp.FindParm(control_group_settings_container_id, "AlphaNpts", "VSPAERO")
    vsp.SetParmValUpdate(alpha_npts_id, 1)

    alpha_start_id = vsp.FindParm(control_group_settings_container_id, "AlphaStart", "VSPAERO")
    vsp.SetParmValUpdate(alpha_start_id, alpha)

    wake_num_iter_id = vsp.FindParm(control_group_settings_container_id, "WakeNumIter", "VSPAERO")
    vsp.SetParmValUpdate(wake_num_iter_id, wake_num_iter)
    vsp.Update()

    print("Підготовка до аналізу (ComputeGeometry)...")
    comp_geom_analysis = "VSPAEROComputeGeometry"
    vsp.SetAnalysisInputDefaults(comp_geom_analysis)
    vsp.SetIntAnalysisInput(comp_geom_analysis, "GeomSet", [vsp.SET_NONE])
    vsp.SetIntAnalysisInput(comp_geom_analysis, 'ThinGeomSet', [vsp.SET_ALL], 0)
    vsp.ExecAnalysis(comp_geom_analysis)

    # path_to_save = results_geo_path + f"_{alpha_start}_{alpha_end}_{deflection_angle}.vsp3";
    # print(f"Збереження налаштувань у файл: {path_to_save}")
    # vsp.WriteVSPFile(path_to_save, vsp.SET_ALL)

    print("Початок аналізу (VSPAEROSweep)...")
    sweep_analysis = "VSPAEROSweep"

    vsp.SetAnalysisInputDefaults(sweep_analysis)
    vsp.SetVSPAERORefWingID(wing_id)
    vsp.PrintAnalysisInputs(sweep_analysis)
    rid = vsp.ExecAnalysis(sweep_analysis)
    print("Аналіз завершено")

    print(vsp.GetStringResults(rid, 'ResultsVec'))
    result = vsp.GetStringResults(rid, 'ResultsVec')[int(1)]
    aoa_vec = vsp.GetDoubleResults(result, 'Alpha')
    cl_vec = vsp.GetDoubleResults(result, 'CLtot')
    cd_vec = vsp.GetDoubleResults(result, 'CDtot')
    cm_vec = vsp.GetDoubleResults(result, 'CMytot')

    vsp.ClearVSPModel()
    return aoa_vec, cl_vec, cd_vec, cm_vec

    # ВИПРАВЛЕНО: Ця функція тепер створює повну структуру для FRuntimeFloatCurve

def create_curve_data(keys_data):
    """
    Створює словник, що представляє FRuntimeFloatCurve з повною структурою FRichCurve.
    """
    # Додаємо додаткові поля, яких вимагає UE для кожної точки (ключа)
    full_keys_data = []
    for key in keys_data:
        full_keys_data.append({
            "InterpMode": "RCIM_Cubic",  # Використовуємо лінійну інтерполяцію
            "TangentMode": "RCTM_Auto",
            "TangentWeightMode": "RCTWM_WeightedNone",
            "Time": float(key["Time"]),
            "Value": float(key["Value"]),
            "ArriveTangent": 0.0,
            "ArriveTangentWeight": 0.0,
            "LeaveTangent": 0.0,
            "LeaveTangentWeight": 0.0,
            "KeyHandlesToIndices": ""
        })

    return {
        "EditorCurveData": {
            "KeyHandlesToIndices": {},
            "Keys": full_keys_data,
            "DefaultValue": 3.402823466e+38,  # Стандартне максимальне значення float
            "PreInfinityExtrap": "RCCE_Constant",
            "PostInfinityExtrap": "RCCE_Constant"
        },
        "ExternalCurve": ""  # Використовуємо None для нульового вказівника
    }

def create_aerodynamic_profile_datatable(rows_data):
    """
    Створює та наповнює DataTable для аеродинамічних профілів.
    При кожному запуску видаляє старий ассет і створює новий для гарантії чистоти даних.
    """
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    # --- Крок 1: Отримання типу структури для рядків DataTable ---
    struct_path = f"/Script/{PROJECT_NAME}.AerodynamicProfileRow"
    row_struct = unreal.load_object(None, struct_path)

    if not row_struct:
        unreal.log_error(f"Не вдалося знайти структуру за шляхом: {struct_path}")
        unreal.log_error("Перевірте, чи правильно ви вказали 'PROJECT_NAME' у скрипті.")
        return

    # --- Крок 2: Видалення існуючого ассету для чистого перезапису ---
    full_asset_path = ASSET_PATH + ASSET_NAME

    if unreal.EditorAssetLibrary.does_asset_exist(full_asset_path):
        unreal.log(f"Ассет '{full_asset_path}' вже існує. Видаляємо для перезапису.")
        if not unreal.EditorAssetLibrary.delete_asset(full_asset_path):
            unreal.log_error(f"Не вдалося видалити існуючий ассет: {full_asset_path}")
            return

    # --- Крок 3: Створення нового ассету DataTable ---
    factory = unreal.DataTableFactory()
    factory.struct = row_struct

    try:
        data_table = asset_tools.create_asset(ASSET_NAME, ASSET_PATH, unreal.DataTable, factory)
        if not data_table:
            raise Exception("Створення ассету повернуло None")
        unreal.log(f"Успішно створено ассет: {data_table.get_path_name()}")
    except Exception as e:
        unreal.log_error(f"Не вдалося створити ассет: {e}")
        return

    # --- Крок 4: Створення даних у вигляді Python об'єктів ---

    # Конвертуємо Python об'єкт у JSON рядок
    json_data_string = json.dumps(rows_data, indent=4)
    unreal.log("Згенеровано JSON з Python об'єктів.")

    # --- Крок 5: Імпорт даних та збереження ассету ---
    try:
        # Використовуємо unreal.DataTableFunctionLibrary.fill_data_table_from_json_string
        result = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(data_table, json_data_string)

        if not result:
            unreal.log_error(f"Не вдалося імпортувати дані в {ASSET_NAME}. Перевірте структуру даних та логи.")
            return

        unreal.log(f"Дані успішно імпортовано в {ASSET_NAME}.")

        unreal.EditorAssetLibrary.save_loaded_asset(data_table)
        unreal.log(f"Ассет {ASSET_NAME} успішно збережено.")
    except Exception as e:
        unreal.log_error(f"Сталася помилка під час імпорту або збереження: {e}")

results = []
for current_deflection_angle in range(int(deflection_angle_start_val), int(deflection_angle_end_val + 1), int(deflection_angle_step_val)):
    short_polar_for_angle = {'re': re, 'alpha': [], 'cl': [], 'cd': [], 'cm': []}
    for alpha_val in range(alpha_start_val, alpha_end_val):
        print(f"Розрахунку для {surface_name}-{sub_surface_index} alpha:{alpha_val} [{alpha_end_val}], flap:{current_deflection_angle} [{deflection_angle_end_val}].")
        aoa_vec_for_flap, cl_vec_for_flap, cd_vec_for_flap, cm_vec_for_flap = calculate(alpha_val,
                                                                                        current_deflection_angle,
                                                                                        wake_num_iter_val,
                                                                                        surface_form)
        short_polar_for_angle['alpha'].append(alpha_val)
        short_polar_for_angle['cl'].append(cl_vec_for_flap[0])
        short_polar_for_angle['cd'].append(cd_vec_for_flap[0])
        short_polar_for_angle['cm'].append(cm_vec_for_flap[0])

    airfoil = Airfoil([Polar(short_polar_for_angle['re'], short_polar_for_angle['alpha'], short_polar_for_angle['cl'],
                             short_polar_for_angle['cd'], short_polar_for_angle['cm'])])
    extrapolated_polar_for_angle = airfoil.extrapolate(cd_max).getPolar(re)

    dirty_cl_vs_aoa = []
    dirty_cd_vs_aoa = []
    dirty_cm_vs_aoa = []
    for index in range(len(extrapolated_polar_for_angle.alpha.tolist())):
        dirty_cl_vs_aoa.append({
            'Time': extrapolated_polar_for_angle.alpha[index].tolist(),
            'Value': extrapolated_polar_for_angle.cl[index].tolist()
        })
        dirty_cd_vs_aoa.append({
            'Time': extrapolated_polar_for_angle.alpha[index].tolist(),
            'Value': extrapolated_polar_for_angle.cd[index].tolist()
        })
        dirty_cm_vs_aoa.append({
            'Time': extrapolated_polar_for_angle.alpha[index].tolist(),
            'Value': extrapolated_polar_for_angle.cm[index].tolist()
        })

    result_for_angle = {"Name": f"Flap_{current_deflection_angle}_Deg",
                        'FlapAngle': current_deflection_angle,
                        'ClVsAoA': create_curve_data(dirty_cl_vs_aoa),
                        'CdVsAoA': create_curve_data(dirty_cd_vs_aoa),
                        'CmVsAoA': create_curve_data(dirty_cm_vs_aoa)}
    results.append(result_for_angle)

print("\n\n############# Результат #############\n\n")
print(results)

print("\n\n############# Створення ассету #############\n\n")
create_aerodynamic_profile_datatable(results)
