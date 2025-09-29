# -*- coding: utf-8 -*-

import openvsp as vsp
import os

vsp.VSPCheckSetup()
vsp.ClearVSPModel()

airfoil_path = r"C:\Users\PC\Documents\Unreal Projects\UAVSimulator\Tools\OpenVSP\airfoils\naca0009.dat"
if not os.path.exists(airfoil_path):
    print(f"ПОПЕРЕДЖЕННЯ: Файл не знайдено за шляхом {airfoil_path}")

wing_id = vsp.AddGeom("WING", "")

vsp.SetParmVal(wing_id, "Span", "XSec_1", 50.0)
vsp.SetParmVal(wing_id, "Root_Chord", "XSec_1", 5.0)
vsp.SetParmVal(wing_id, "Tip_Chord", "XSec_1", 5.0)

vsp.SplitWingXSec(wing_id, 1)

vsp.SetParmVal(wing_id, "Root_Chord", "XSec_2", 5.0)
vsp.SetParmVal(wing_id, "Tip_Chord", "XSec_2", 1.0)

vsp.Update()

xsec_surf_id = vsp.GetXSecSurf(wing_id, 0)

vsp.ChangeXSecShape(xsec_surf_id, 0, vsp.XS_FILE_AIRFOIL)
root_xsec_id = vsp.GetXSec(xsec_surf_id, 0)
res = vsp.ReadFileAirfoil(root_xsec_id, airfoil_path)

tip_xsec_id = vsp.GetXSec(xsec_surf_id, 1)
vsp.ChangeXSecShape(xsec_surf_id, 1, vsp.XS_FILE_AIRFOIL)
vsp.ReadFileAirfoil(tip_xsec_id, airfoil_path)

vsp.Update()


vsp.WriteVSPFile(r"C:\Users\PC\Documents\Unreal Projects\UAVSimulator\Tools\OpenVSP\result\wing.vsp3", vsp.SET_ALL)

print("Скрипт успішно виконано.")
