@echo off
setlocal

REM Open Octave Windows launcher
REM Starts controller and frontend in separate Command Prompt windows.

set PROJECT_ROOT=%~dp0

echo Starting Open Octave...
echo.

REM Start backend controller
start "Open Octave Controller" cmd /k "cd /d "%PROJECT_ROOT%software\controller" && npm run start:laptop"

REM Give backend a moment to start
timeout /t 2 /nobreak >nul

REM Start frontend
start "Open Octave Frontend" cmd /k "cd /d "%PROJECT_ROOT%software\web" && npm run dev"

REM Give frontend a moment to start
timeout /t 4 /nobreak >nul

REM Open the UI
start "" "http://localhost:5173"

echo Open Octave launched.
echo.
echo Before powering the ESP32, make sure:
echo 1. Windows Mobile Hotspot is ON
echo 2. Hotspot name is Open Octave
echo 3. Password is oop321321
echo 4. Network band is 2.4 GHz
echo.
pause