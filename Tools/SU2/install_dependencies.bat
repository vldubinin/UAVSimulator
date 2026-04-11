@echo off
:: Use quotes around the entire set command to handle spaces correctly
set "PYTHON_EXE=C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"

echo [1/4] Checking Python path...

:: Using double quotes around the variable is critical here
if not exist "%PYTHON_EXE%" goto :ERROR_NO_PYTHON

echo [2/4] Updating pip...
"%PYTHON_EXE%" -m pip install --upgrade pip

echo [3/4] Installing dependencies (gmsh, pandas, numpy, matplotlib)...
"%PYTHON_EXE%" -m pip install gmsh pandas numpy matplotlib

echo [4/4] Installing Microsoft MPI via winget...
winget install Microsoft.msmpi --accept-package-agreements --accept-source-agreements

echo.
echo ======================================================
echo Installation completed successfully!
echo Note: SU2_CFD must be extracted separately from SU2-v8.4.0-win64.zip as SU2-v8.4.0-win64/bin
echo ======================================================
pause
exit /b

:ERROR_NO_PYTHON
echo.
echo ERROR: Python not found at:
echo "%PYTHON_EXE%"
echo.
echo Please check if UE_5.7 is the correct folder name.
pause
exit /b