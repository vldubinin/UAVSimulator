@echo off
setlocal

:: Встановлюємо змінні для шляхів
set "PYTHON_DIR=C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\ThirdParty\Python3\Win64"
set "PYTHON_EXE=%PYTHON_DIR%\python.exe"
set "OPENVSP_PYTHON_ROOT=%~dp0\OpenVSP-3.45.4-win64\python"

:: Переконуємося, що шлях до Python існує
if not exist "%PYTHON_EXE%" (
    echo "Error: Python executable not found at %PYTHON_EXE%"
    echo "Please check the PYTHON_DIR variable."
    goto :end
)

:: Встановлення залежностей через pip
pushd "%OPENVSP_PYTHON_ROOT%"

echo "Running pip install from requirements.txt..."
echo "Using Python from: %PYTHON_EXE%"
echo "Working directory: %cd%"

if not exist "requirements.txt" (
    echo "Error: requirements.txt not found in %cd%"
) else (
    "%PYTHON_EXE%" -m pip install -r requirements.txt
)

popd
echo.

:: --- Копіювання пакетів OpenVSP ---
echo "Copying OpenVSP packages to site-packages..."
call :copyPackage "openvsp\openvsp"     "openvsp"
call :copyPackage "utilities\utilities" "utilities"
call :copyPackage "AvlPy\avlpy"         "avlpy"
call :copyPackage "CHARM\charm"         "charm"
call :copyPackage "degen_geom\degen_geom"         "degen_geom"
call :copyPackage "openvsp_config\openvsp_config"         "openvsp_config"
call :copyPackage "pyPMARC\pypmarc"         "pypmarc"
call :copyPackage "vsp_airfoils\vsp_airfoils"         "vsp_airfoils"

echo.
echo "Script finished successfully."
goto :end

:: --- Підпрограма для копіювання директорії ---
:copyPackage
:: %1 - Вихідний підшлях (напр. "openvsp\openvsp")
:: %2 - Назва пакету призначення (напр. "openvsp")
set "SOURCE_SUBPATH=%~1"
set "DEST_PKG_NAME=%~2"

set "SOURCE_DIR=%OPENVSP_PYTHON_ROOT%\%SOURCE_SUBPATH%"
set "DEST_DIR=%PYTHON_DIR%\Lib\site-packages\%DEST_PKG_NAME%"

echo.
echo Processing package: %DEST_PKG_NAME%
if not exist "%SOURCE_DIR%" (
    echo   Warning: Source directory not found. Skipping: %SOURCE_DIR%
    goto :eof
)

echo   Source: %SOURCE_DIR%
echo   Destination: %DEST_DIR%

xcopy "%SOURCE_DIR%" "%DEST_DIR%\" /S /E /Y /I /Q > nul
echo   Copied successfully.
goto :eof
:: --- Кінець підпрограми ---


:end
pause
endlocal