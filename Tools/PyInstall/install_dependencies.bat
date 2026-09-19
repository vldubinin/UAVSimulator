@echo off
:: Встановлює Python-залежності проєкту (Tools/PyInstall/requirements.txt)
:: у вбудований Python3, що постачається разом з Unreal Engine.
set "PYTHON_EXE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"

echo [1/3] Checking Python path...

if not exist "%PYTHON_EXE%" goto :ERROR_NO_PYTHON

echo [2/3] Updating pip...
"%PYTHON_EXE%" -m pip install --upgrade pip

echo [3/3] Installing dependencies from requirements.txt...
"%PYTHON_EXE%" -m pip install -r "%~dp0requirements.txt"

echo.
echo ======================================================
echo Installation completed successfully!
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
