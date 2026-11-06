@echo off
setlocal
cd /d "%~dp0"

rem Use the Python Launcher when available, otherwise fall back to python on PATH.
where py >nul 2>&1
if not errorlevel 1 (
    set "PYTHON=py -3"
) else (
    where python >nul 2>&1
    if errorlevel 1 (
        echo ERROR: Python 3 was not found.
        echo Install Python 3.9 or newer, then run this file again.
        pause
        exit /b 1
    )
    set "PYTHON=python"
)

rem Check that the client dependencies are installed before starting.
%PYTHON% -c "import mss, PIL, zeroconf" >nul 2>&1
if errorlevel 1 goto missing_dependencies

%PYTHON% mirror.py %*
set "EXIT_CODE=%ERRORLEVEL%"
if not "%EXIT_CODE%"=="0" (
    echo.
    echo Mirror client exited with code %EXIT_CODE%.
    pause
)
exit /b %EXIT_CODE%

:missing_dependencies
echo ERROR: One or more PC-client dependencies are missing.
echo Install them with:
echo   %PYTHON% -m pip install -r requirements.txt
echo.
pause
exit /b 1
