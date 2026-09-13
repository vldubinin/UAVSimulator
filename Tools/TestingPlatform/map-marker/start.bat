@echo off
REM ---------------------------------------------------------------------------
REM Run the interactive object-marking map (map_object_marker.py)
REM ---------------------------------------------------------------------------
setlocal

set "PY=python"
where %PY% >nul 2>&1 || set "PY=py"

cd /d "%~dp0"
%PY% map_object_marker.py
pause
