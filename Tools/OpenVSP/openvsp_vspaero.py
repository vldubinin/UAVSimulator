import openvsp as vsp
import os

output_dir = r"C:\Users\PC\Documents\Unreal Projects\UAVSimulator\Tools\OpenVSP\result"
os.makedirs(output_dir, exist_ok=True)

alpha_start_val = -8.0  # початковий AoA
alpha_end_val = 10.0  # кінцевий AoA
alpha_npts_val = 19.0  # кількість значення AoA в полярі.
deflection_angle_start_val = -45  # початковий flap angle
deflection_angle_end_val = 45  # кінцевий flap angle
deflection_angle_step_val = 5  # крок flap angle
wake_num_iter_val = 3  # точність, мінімум 3

surface_form = [{"span": 5, "root_chord": 4, "tip_chord": 4, "sweep": 10.0, "flap": True},
                {"span": 5, "root_chord": 4, "tip_chord": 2, "sweep": 10.0, "flap": False}]

vsp.VSPCheckSetup()

def calculate(alpha_start, alpha_end, deflection_angle, alpha_npts, wake_num_iter, surface_geometry):
    #print(f"Початок розрахунку для alpha_start:{alpha_start}, alpha_end:{alpha_end}, deflection_angle:{deflection_angle}")

    results_geo_path = os.path.join(output_dir, "geo_res.vsp3")

    vsp.ClearVSPModel()
    #print("Створення геометрії")
    wing_id = vsp.AddGeom('WING', '')
    vsp.SetParmVal(vsp.FindParm(wing_id, "Sym_Planar_Flag", "Sym"), 0.0)

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

    #print("Задання кутів атаки та положення закрилків")
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


    #print("Підготовка до аналізу (ComputeGeometry)...")
    comp_geom_analysis = "VSPAEROComputeGeometry"
    vsp.SetAnalysisInputDefaults(comp_geom_analysis)
    vsp.SetIntAnalysisInput(comp_geom_analysis, "GeomSet", [vsp.SET_NONE])
    vsp.SetIntAnalysisInput(comp_geom_analysis, 'ThinGeomSet', [vsp.SET_ALL], 0)
    #vsp.ExecAnalysis(comp_geom_analysis)

    #print(f"Збереження налаштувань у файл: {results_geo_path}")

    vsp.WriteVSPFile(results_geo_path, vsp.SET_ALL)

    #print("Початок аналізу (VSPAEROSweep)...")
    sweep_analysis = "VSPAEROSweep"

    vsp.SetAnalysisInputDefaults(sweep_analysis)
    vsp.SetVSPAERORefWingID(wing_id)
    vsp.PrintAnalysisInputs(sweep_analysis)
    rid = vsp.ExecAnalysis(sweep_analysis)
    #print("Аналіз завершено")

    result = vsp.GetStringResults(rid, 'ResultsVec')[int(alpha_npts)]
    aoa_vec = vsp.GetDoubleResults(result, 'Alpha')
    cl_vec = vsp.GetDoubleResults(result, 'CLtot')
    cd_vec = vsp.GetDoubleResults(result, 'CDtot')
    cm_vec = vsp.GetDoubleResults(result, 'CMytot')

    vsp.ClearVSPModel()
    return aoa_vec, cl_vec, cd_vec, cm_vec

results = []
for current_deflection_angle in range(int(deflection_angle_start_val), int(deflection_angle_end_val),
                                      int(deflection_angle_step_val)):
    aoa_vec_for_flap, cl_vec_for_flap, cd_vec_for_flap, cm_vec_for_flap = calculate(alpha_start_val, alpha_end_val,
                                                                                    current_deflection_angle,
                                                                                    alpha_npts_val, wake_num_iter_val,
                                                                                    surface_form)
    result_for_angle = {'FlapAngle': current_deflection_angle, 'Polar': []}

    for index, aoa in enumerate(aoa_vec_for_flap):
        result_for_angle['Polar'].append(
            {'AoA': aoa, 'Cl': cl_vec_for_flap[index], 'Cd': cd_vec_for_flap[index], 'Cm': cm_vec_for_flap[index]})

    results.append(result_for_angle)

print("############# Результат #############")
print(results)
