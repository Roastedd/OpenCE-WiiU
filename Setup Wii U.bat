@echo off
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (
    py -3 setup_wiiu.py %*
) else (
    python setup_wiiu.py %*
)
if errorlevel 1 (
    echo.
    echo If Python was not found, install version 3.10 or newer from https://www.python.org/downloads/
    echo and tick "Add python.exe to PATH" in the installer, then open this file again.
    pause
)
