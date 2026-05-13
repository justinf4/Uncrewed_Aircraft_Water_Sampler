@echo off
:: monitor.bat — open Python serial monitor (runs until Ctrl+C)
cd /d "%~dp0.."
set PORT=COM11

:: Check pyserial is available
python -c "import serial" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [FAIL] pyserial not installed. Run: pip install pyserial
    exit /b 1
)

echo [MONITOR] Opening serial monitor on %PORT% @ 115200 baud
echo           Press Ctrl+C to stop.
echo.
python scripts\monitor.py %PORT% 115200
