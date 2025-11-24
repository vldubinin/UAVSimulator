import unreal
import json

# --- КОНФІГУРАЦІЯ ---
# Обов'язково замініть "MyProject" на назву вашого проекту.
# Назва проекту - це назва файлу .uproject (без розширення).
PROJECT_NAME = "UAVSimulator"
ASSET_PATH = "/Game/DataTables/"
ASSET_NAME = "DT_AerodynamicProfile"
# --------------------

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
                "KeyHandlesToIndices":""
            })

        return {
            "EditorCurveData": {
                "KeyHandlesToIndices": {},
                "Keys": full_keys_data,
                "DefaultValue": 3.402823466e+38, # Стандартне максимальне значення float
                "PreInfinityExtrap": "RCCE_Constant",
                "PostInfinityExtrap": "RCCE_Constant"
            },
            "ExternalCurve": "" # Використовуємо None для нульового вказівника
        }

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


# Визначаємо дані для рядків у вигляді списку словників
rows_data = [
        {
            "Name": "Flap_0_Deg",
            "FlapAngle": 0.0,
            "ClVsAoA": create_curve_data([
                {"Time": -15.0, "Value": -0.8},
                {"Time": 0.0, "Value": 0.2},
                {"Time": 15.0, "Value": 1.4}
            ]),
            "CdVsAoA": create_curve_data([
                {"Time": -15.0, "Value": 0.1},
                {"Time": 0.0, "Value": 0.02},
                {"Time": 15.0, "Value": 0.12}
            ]),
            "CmVsAoA": create_curve_data([
                {"Time": -15.0, "Value": -0.05},
                {"Time": 0.0, "Value": 0.0},
                {"Time": 15.0, "Value": 0.05}
            ])
        },
        {
            "Name": "Flap_15_Deg",
            "FlapAngle": 15.0,
            "ClVsAoA": create_curve_data([
                {"Time": -15.0, "Value": 0.2},
                {"Time": 0.0, "Value": 0.8},
                {"Time": 15.0, "Value": 1.9}
            ]),
            "CdVsAoA": create_curve_data([
                {"Time": -15.0, "Value": 0.15},
                {"Time": 0.0, "Value": 0.08},
                {"Time": 15.0, "Value": 0.2}
            ]),
            "CmVsAoA": create_curve_data([
                {"Time": -15.0, "Value": -0.1},
                {"Time": 0.0, "Value": -0.05},
                {"Time": 15.0, "Value": 0.0}
            ])
        }
    ]
# Запуск функції
create_aerodynamic_profile_datatable(rows_data)
