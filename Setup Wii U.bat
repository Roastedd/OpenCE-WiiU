@echo off
cd /d "%~dp0"
where py >nul 2>nul
if not errorlevel 1 (
    py -3 setup_wiiu.py %*
) else (
    python setup_wiiu.py %*
)
if errorlevel 1 (
    echo Install Python 3.10 or later from python.org if Python was not found.
    pause
)
