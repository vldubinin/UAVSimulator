# -*- coding: utf-8 -*-

import openvsp as vsp
import os
import csv
import matplotlib.pyplot as plt


def setup_geometry(results_geo_path):
    """Створює геометрію крила NACA 0012."""
    vsp.VSPCheckSetup()
    vsp.ClearVSPModel()

    print("1. Створення геометрії крила...")
    wing_id = vsp.AddGeom('WING', '')

    # Налаштування секції крила
    vsp.SetDriverGroup(wing_id, 1, vsp.AR_WSECT_DRIVER, vsp.ROOTC_WSECT_DRIVER, vsp.TIPC_WSECT_DRIVER)
    vsp.Update()

    # Встановлення параметрів профілю та геометрії
    vsp.SetParmVal(wing_id, 'ThickChord', 'XSecCurve_0', 0.12)
    vsp.SetParmVal(wing_id, 'ThickChord', 'XSecCurve_1', 0.12)
    vsp.SetParmVal(wing_id, 'Root_Chord', 'XSec_1', 1.0)
    vsp.SetParmVal(wing_id, 'Tip_Chord', 'XSec_1', 1.0)
    vsp.SetParmVal(wing_id, 'Aspect', 'XSec_1', 5.0)
    vsp.SetParmVal(wing_id, 'Sweep', 'XSec_1', 30.0)

    # Налаштування сітки для аналізу
    vsp.SetParmVal(wing_id, 'TECluster', 'WingGeom', 1.0)
    vsp.SetParmVal(wing_id, 'LECluster', 'WingGeom', 0.2)
    vsp.SetParmVal(wing_id, 'OutCluster', 'XSec_1', 1.0)
    vsp.SetParmVal(wing_id, 'SectTess_U', 'XSec_1', 41)
    vsp.SetParmVal(wing_id, 'Tess_W', 'Shape', 51)
    vsp.Update()

    # ################### ПОЧАТОК КОДУ ДЛЯ ЗАКРИЛКА ###################

    # Додаємо SubSurface типу "Control Surface" до крила
    # Це створює закрилок (або елерон/передкрилок)
    flap_id = vsp.AddSubSurf(wing_id, vsp.SS_CONTROL)

    # Налаштовуємо положення та розмір закрилка
    # U - вздовж хорди (0.0 = передня кромка, 1.0 = задня кромка)
    # W - вздовж розмаху (0.0 = корінь, 1.0 = законцівка)

    # Закрилок починається з 75% хорди
    vsp.SetParmVal(flap_id, "Root_U_Start", "SubSurf", 0.75)
    vsp.SetParmVal(flap_id, "Tip_U_Start", "SubSurf", 0.75)

    # Закрилок закінчується на задній кромці (100% хорди)
    vsp.SetParmVal(flap_id, "Root_U_End", "SubSurf", 1.0)
    vsp.SetParmVal(flap_id, "Tip_U_End", "SubSurf", 1.0)

    # Закрилок займає проміжок від 10% до 60% напіврозмаху
    vsp.SetParmVal(flap_id, "Root_W_Start", "SubSurf", 0.1)
    vsp.SetParmVal(flap_id, "Tip_W_Start", "SubSurf", 0.1)
    vsp.SetParmVal(flap_id, "Root_W_End", "SubSurf", 0.6)
    vsp.SetParmVal(flap_id, "Tip_W_End", "SubSurf", 0.6)
    vsp.Update()
    print("   Додано закрилок до геометрії.")
    vsp.SetParmVal(wing_id, 'S_Deflection', 'SubSurf_1', 45)
    # ################### КІНЕЦЬ КОДУ ДЛЯ ЗАКРИЛКА ###################
    vsp.Update()
    print("   Геометрію створено.\n")
    vsp.WriteVSPFile(results_geo_path, vsp.SET_ALL)
    return wing_id


def run_vspaero_sweep(wing_id, alpha_start, alpha_end, num_alphas, results_csv_path, flap_angle):
    """Налаштовує та запускає аналіз VSPAERO Sweep."""

    # 2. Підготовка до аналізу
    print("2. Підготовка до аналізу (ComputeGeometry)...")
    comp_geom_analysis = "VSPAEROComputeGeometry"
    vsp.SetAnalysisInputDefaults(comp_geom_analysis)
    vsp.SetIntAnalysisInput(comp_geom_analysis, 'ThinGeomSet', [vsp.SET_ALL], 0)
    vsp.ExecAnalysis(comp_geom_analysis)
    print("   Підготовку завершено.\n")

    # 3. Налаштування та запуск VSPAERO Sweep
    print(f"3. Налаштування VSPAERO Sweep від {alpha_start} до {alpha_end} градусів...")
    sweep_analysis = "VSPAEROSweep"
    vsp.SetAnalysisInputDefaults(sweep_analysis)

    # Встановлення вхідних параметрів
    vsp.SetIntAnalysisInput(sweep_analysis, 'ThinGeomSet', [vsp.SET_ALL], 0)
    vsp.SetIntAnalysisInput(sweep_analysis, 'RefFlag', [1], 0)
    vsp.SetIntAnalysisInput(sweep_analysis, 'Symmetry', [1], 0)
    vsp.SetDoubleAnalysisInput(sweep_analysis, 'MachStart', [0.1], 0)
    vsp.SetIntAnalysisInput(sweep_analysis, 'MachNpts', [1], 0)
    vsp.SetIntAnalysisInput(sweep_analysis, 'WakeNumIter', [5], 0)

    # Налаштування діапазону кутів атаки
    vsp.SetDoubleAnalysisInput(sweep_analysis, 'AlphaStart', [alpha_start], 0)
    vsp.SetDoubleAnalysisInput(sweep_analysis, 'AlphaEnd', [alpha_end], 0)
    vsp.SetIntAnalysisInput(sweep_analysis, 'AlphaNpts', [num_alphas], 0)
    vsp.Update()

    # ################### ПОЧАТОК КОДУ ДЛЯ ВІДХИЛЕННЯ ЗАКРИЛКА ###################

    # Встановлюємо кут відхилення закрилка для аналізу
    # 'S_Deflection' - це параметр для відхилення, 'SubSurf_1' - ім'я за замовчуванням для першого SubSurface
    vsp.SetParmVal(wing_id, 'S_Deflection', 'SubSurf_1', flap_angle)
    vsp.Update()
    print(f"   Встановлено кут відхилення закрилка: {flap_angle} градусів.")

    # ################### КІНЕЦЬ КОДУ ДЛЯ ВІДХИЛЕННЯ ЗАКРИЛКА ###################

    print("   Запуск аналізу...")
    rid = vsp.ExecAnalysis(sweep_analysis)
    print("   Аналіз завершено.\n")

    # Збереження результатів у CSV файл
    vsp.WriteResultsCSVFile(rid, results_csv_path)
    print(f"   Результати збережено у файл: {results_csv_path}\n")
    return rid

def parse_results_from_csv(rid, alpha_start, alpha_end):
    results_id = vsp.GetStringResults(rid, 'ResultsVec')[int((alpha_end - alpha_start) + 1)]
    aoa_vec = vsp.GetDoubleResults(results_id, 'Alpha')
    cl_vec = vsp.GetDoubleResults(results_id, 'CLtot')
    cd_vec = vsp.GetDoubleResults(results_id, 'CDtot')
    cm_vec = vsp.GetDoubleResults(results_id, 'CMytot')

    results = []
    for index, aoa in enumerate(aoa_vec):
        results.append({'AoA': aoa, 'Cl': cl_vec[index], 'Cd': cd_vec[index], 'Cm': cm_vec[index]})

    print(f"{results_id} #########################################")
    print(results)
    return results


def plot_results(results):
    """Будує графіки аеродинамічних коефіцієнтів."""
    if not results:
        print("Немає даних для побудови графіків.")
        return

    print("5. Побудова графіків...")
    plt.style.use('seaborn-v0_8-whitegrid')
    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(18, 5))
    fig.suptitle('Аеродинамічні характеристики крила', fontsize=16)

    # CL vs Alpha
    ax1.plot(results['alpha'], results['cl'], 'o-', color='b', label='CL')
    ax1.set_title('$C_L$ vs $\\alpha$')
    ax1.set_xlabel('Кут атаки, $\\alpha$ (град)')
    ax1.set_ylabel('Коефіцієнт підіймальної сили, $C_L$')

    # CD vs Alpha
    ax2.plot(results['alpha'], results['cd'], 'o-', color='r', label='CD')
    ax2.set_title('$C_D$ vs $\\alpha$')
    ax2.set_xlabel('Кут атаки, $\\alpha$ (град)')
    ax2.set_ylabel('Коефіцієнт опору, $C_D$')

    # Cm vs Alpha
    ax3.plot(results['alpha'], results['cm'], 'o-', color='g', label='Cm')
    ax3.set_title('$C_m$ vs $\\alpha$')
    ax3.set_xlabel('Кут атаки, $\\alpha$ (град)')
    ax3.set_ylabel('Коефіцієнт моменту, $C_m$')

    for ax in [ax1, ax2, ax3]:
        ax.legend()
        ax.grid(True)

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    plt.show()
    print("   Графіки побудовано.\n")


def main():
    """Головна функція для запуску всього процесу."""
    # --- Налаштування ---
    # Встановлюємо вказаний шлях для збереження результатів
    output_dir = r"C:\Users\PC\Documents\Unreal Projects\UAVSimulator\Tools\OpenVSP\result"
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    results_csv_path = os.path.join(output_dir, "res.csv")
    results_geo_path = os.path.join(output_dir, "geo_res.vsp3")

    alpha_start = -5.0
    alpha_end = 15.0
    num_alphas = 21  # Кількість точок (крок буде 1 градус)
    start_flap_angle = -10.0
    end_flap_angle = 10.0
    flap_angle_step = 10.0

    # --- Запуск процесів ---
    wing_id = setup_geometry(results_geo_path)

    """flap_angle = start_flap_angle
    while flap_angle <= end_flap_angle:
        rid = run_vspaero_sweep(wing_id, alpha_start, alpha_end, num_alphas, results_csv_path, flap_angle)
        print(f'Angle{flap_angle}')
        parse_results_from_csv(rid, alpha_start, alpha_end)
        flap_angle += flap_angle_step
"""
    vsp.ClearVSPModel()
    print("Скрипт успішно виконано.")


if __name__ == "__main__":
    main()

