@echo off
:: upload_and_monitor.bat — build, upload, then open monitor
cd /d "%~dp0.."
set PORT=COM11

echo [UPLOAD] Building and uploading to %PORT%...
call scripts\upload.bat
if %ERRORLEVEL% NEQ 0 exit /b 1

echo.
echo [MONITOR] Opening serial monitor — Ctrl+C to stop.
echo.
python scripts\monitor.py %PORT% 115200
