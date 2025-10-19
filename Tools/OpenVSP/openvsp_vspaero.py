import openvsp as vsp
from airfoilprep import Airfoil
from airfoilprep import Polar
import json
import unreal
import os

output_file = r"C:\Users\PC\Documents\Unreal Projects\UAVSimulator\Tools\OpenVSP\result\output.json"
airfoil_path = r"C:\Users\PC\Documents\Unreal Projects\UAVSimulator\Tools\OpenVSP\airfoils\naca0009.dat"

alpha_start_val = -8.0  # початковий AoA
alpha_end_val = 10.0  # кінцевий AoA
alpha_npts_val = 19.0  # кількість значення AoA в полярі.
deflection_angle_start_val = -2  # початковий flap angle
deflection_angle_end_val = 2  # кінцевий flap angle
deflection_angle_step_val = 1  # крок flap angle
wake_num_iter_val = 3  # точність, мінімум 3

re = 750000.0
cd_max = 1.25

surface_form = [{"span": 5, "root_chord": 4, "tip_chord": 4, "sweep": 10.0, "flap": True},
                {"span": 5, "root_chord": 4, "tip_chord": 2, "sweep": 10.0, "flap": False}]

vsp.VSPCheckSetup()

def calculate(alpha_start, alpha_end, deflection_angle, alpha_npts, wake_num_iter, surface_geometry):
    print(f"Початок розрахунку для alpha_start:{alpha_start}, alpha_end:{alpha_end}, deflection_angle:{deflection_angle}")


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

    deflection_angle_id = vsp.FindParm(control_group_settings_container_id, "DeflectionAngle", "ControlSurfaceGroup_0")
    vsp.SetParmValUpdate(deflection_angle_id, deflection_angle)

    alpha_npts_id = vsp.FindParm(control_group_settings_container_id, "AlphaNpts", "VSPAERO")
    vsp.SetParmValUpdate(alpha_npts_id, alpha_npts)

    alpha_start_id = vsp.FindParm(control_group_settings_container_id, "AlphaStart", "VSPAERO")
    vsp.SetParmValUpdate(alpha_start_id, alpha_start)

    alpha_end_id = vsp.FindParm(control_group_settings_container_id, "AlphaEnd", "VSPAERO")
    vsp.SetParmValUpdate(alpha_end_id, alpha_end)

    wake_num_iter_id = vsp.FindParm(control_group_settings_container_id, "WakeNumIter", "VSPAERO")
    vsp.SetParmValUpdate(wake_num_iter_id, wake_num_iter)
    vsp.Update()


    print("Підготовка до аналізу (ComputeGeometry)...")
    comp_geom_analysis = "VSPAEROComputeGeometry"
    vsp.SetAnalysisInputDefaults(comp_geom_analysis)
    vsp.SetIntAnalysisInput(comp_geom_analysis, "GeomSet", [vsp.SET_NONE])
    vsp.SetIntAnalysisInput(comp_geom_analysis, 'ThinGeomSet', [vsp.SET_ALL], 0)
    vsp.ExecAnalysis(comp_geom_analysis)

    #print(f"Збереження налаштувань у файл: {results_geo_path}")

    #vsp.WriteVSPFile(results_geo_path, vsp.SET_ALL)

    print("Початок аналізу (VSPAEROSweep)...")
    sweep_analysis = "VSPAEROSweep"

    vsp.SetAnalysisInputDefaults(sweep_analysis)
    vsp.SetVSPAERORefWingID(wing_id)
    vsp.PrintAnalysisInputs(sweep_analysis)
    rid = vsp.ExecAnalysis(sweep_analysis)
    print("Аналіз завершено")

    print(vsp.GetStringResults(rid, 'ResultsVec'))
    result = vsp.GetStringResults(rid, 'ResultsVec')[int(alpha_npts)]
    aoa_vec = vsp.GetDoubleResults(result, 'Alpha')
    cl_vec = vsp.GetDoubleResults(result, 'CLtot')
    cd_vec = vsp.GetDoubleResults(result, 'CDtot')
    cm_vec = vsp.GetDoubleResults(result, 'CMytot')

    vsp.ClearVSPModel()
    return aoa_vec, cl_vec, cd_vec, cm_vec

results = []
for current_deflection_angle in range(int(deflection_angle_start_val), int(deflection_angle_end_val + 1),
                                      int(deflection_angle_step_val)):
    aoa_vec_for_flap, cl_vec_for_flap, cd_vec_for_flap, cm_vec_for_flap = calculate(alpha_start_val, alpha_end_val,
                                                                                    current_deflection_angle,
                                                                                    alpha_npts_val, wake_num_iter_val,
                                                                                    surface_form)

    short_polar_for_angle = {'re': re, 'alpha':[], 'cl':[], 'cd':[], 'cm':[] }

    for index, aoa in enumerate(aoa_vec_for_flap):
        short_polar_for_angle['alpha'].append(aoa)
        short_polar_for_angle['cl'].append(cl_vec_for_flap[index])
        short_polar_for_angle['cd'].append(cd_vec_for_flap[index])
        short_polar_for_angle['cm'].append(cm_vec_for_flap[index])

    airfoil = Airfoil([Polar(short_polar_for_angle['re'], short_polar_for_angle['alpha'], short_polar_for_angle['cl'], short_polar_for_angle['cd'], short_polar_for_angle['cm'])])
    extrapolated_polar_for_angle = airfoil.extrapolate(cd_max).getPolar(re)

    result_for_angle = {'FlapAngle': current_deflection_angle, 'ClVsAoA':[], 'CdVsAoA':[], 'CmVsAoA':[] }
    for index in range(len(extrapolated_polar_for_angle.alpha.tolist())):
        result_for_angle['ClVsAoA'].append({
            'alpha': extrapolated_polar_for_angle.alpha[index].tolist(),
            'cl': extrapolated_polar_for_angle.cl[index].tolist()
        })
        result_for_angle['CdVsAoA'].append({
            'alpha': extrapolated_polar_for_angle.alpha[index].tolist(),
            'cd': extrapolated_polar_for_angle.cd[index].tolist()
        })
        result_for_angle['CmVsAoA'].append({
            'alpha': extrapolated_polar_for_angle.alpha[index].tolist(),
            'cm': extrapolated_polar_for_angle.cm[index].tolist()
        })
    results.append(result_for_angle)



try:
    with open(output_file, 'w', encoding='utf-8') as json_file:
        # Use json.dump() to write the Python dictionary to the file.
        # 'indent=4' makes the JSON file human-readable by adding indentation.
        json.dump(results, json_file, indent=4)

    print(f"Successfully created and wrote to {output_file}")

except IOError as e:
    print(f"An error occurred while writing the file: {e}")

print("\n\n############# Результат #############\n\n")
print(results)