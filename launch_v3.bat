@echo off
setlocal EnableDelayedExpansion
title Smart PLC over WiFi (MS CP V3) - Production Launcher
color 0A

echo =====================================================================
echo    SMART PLC OVER WIFI (MS CP V3) - INDUSTRIAL SUPERVISORY SYSTEM   
echo =====================================================================
echo.

:: 1. Check Python Availability
python --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Python is not found in PATH!
    echo Please install Python 3.11+ and ensure 'Add Python to PATH' is checked.
    pause
    exit /b 1
)

:: 2. Locate or Activate Virtual Environment
if exist ".venv\Scripts\activate.bat" (
    echo [OK] Using root virtual environment (.venv)
    call .venv\Scripts\activate.bat
) else if exist "gateway\.venv\Scripts\activate.bat" (
    echo [OK] Using gateway virtual environment (gateway\.venv)
    call gateway\.venv\Scripts\activate.bat
) else (
    echo [INFO] Virtual environment not detected. Creating .venv...
    python -m venv .venv
    call .venv\Scripts\activate.bat
    echo [INFO] Installing gateway requirements...
    pip install -r gateway\requirements.txt
)

:: 3. Operator Menu
echo.
echo Select an operation mode:
echo   [1] Start Python Gateway + Open Dashboard (Normal Production Mode)
echo   [2] Start Local Static Web Dashboard Server (Port 8080)
echo   [3] Run System Automated Tests (Offline Verification)
echo   [4] Open Dashboard HTML Directly in Browser
echo   [5] Exit
echo.
set /p CHOICE="Enter choice [1-5] (default is 1): "

if "%CHOICE%"=="" set CHOICE=1

if "%CHOICE%"=="1" (
    echo.
    echo [LAUNCH] Starting Python MC-Protocol / Serial Gateway...
    start "Smart PLC Web Dashboard" http://localhost:8080/
    start "Local HTTP Dashboard Server" cmd /k "python -m http.server 8080 --directory web"
    cd gateway
    python gateway.py
    pause
    exit /b 0
)

if "%CHOICE%"=="2" (
    echo.
    echo [LAUNCH] Starting Static Dashboard on http://localhost:8080/ ...
    start http://localhost:8080/
    python -m http.server 8080 --directory web
    exit /b 0
)

if "%CHOICE%"=="3" (
    echo.
    echo [TEST] Running Automated Python and Browser Tests...
    python -m unittest discover -s tests -v
    python gateway\test_unit.py
    where node >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        node tests\run_browser_tests.js
    ) else (
        echo [INFO] Node.js not found in PATH; skipping headless browser tests.
    )
    echo.
    echo Tests completed.
    pause
    exit /b 0
)

if "%CHOICE%"=="4" (
    echo.
    echo [LAUNCH] Opening web\index.html in default browser...
    start "" "web\index.html"
    exit /b 0
)

if "%CHOICE%"=="5" (
    exit /b 0
)

echo Invalid choice. Exiting.
pause
