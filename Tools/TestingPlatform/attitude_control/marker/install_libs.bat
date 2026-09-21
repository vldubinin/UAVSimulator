@echo off
REM ---------------------------------------------------------------------------
REM Install the Python dependencies for map_object_marker.py
REM ---------------------------------------------------------------------------
setlocal

set "PY=python"
where %PY% >nul 2>&1 || set "PY=py"

echo Upgrading pip...
%PY% -m pip install --upgrade pip

echo.
echo Installing libraries (tkintermapview + its deps Pillow, requests)...
%PY% -m pip install tkintermapview

echo.
echo Done. tkinter ships with the standard Python distribution for Windows.
pause
