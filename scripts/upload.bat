@echo off
:: upload.bat — build + upload to board
:: Auto-detects COM port if COM11 is not present
cd /d "%~dp0.."

:: Check if preferred port exists, fall back to auto-detect
set PORT=COM11
python -c "import serial.tools.list_ports; ports=[p.device for p in serial.tools.list_ports.comports()]; exit(0 if 'COM11' in ports else 1)" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [WARN] COM11 not found. Detecting port automatically...
    for /f "delims=" %%p in ('python -c "import serial.tools.list_ports; ports=serial.tools.list_ports.comports(); print(ports[0].device) if ports else print()"') do set PORT=%%p
    if "!PORT!"=="" (
        echo [FAIL] No serial port found. Is the board plugged in?
        exit /b 1
    )
    echo [INFO] Using port: %PORT%
)

echo [UPLOAD] Building and uploading to %PORT%...
pio run --target upload --upload-port %PORT%
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [FAIL] Upload failed. Common causes:
    echo        1. Port busy   -- close any open monitor windows first, then retry
    echo        2. DFU mode    -- DOUBLE-TAP the RESET button on the board quickly,
    echo                          then re-run upload within ~5 seconds
    echo        3. Cable       -- use a data USB-C cable, not a charge-only cable
    echo        4. Port gone   -- press RESET once to re-enumerate, check Device Manager
    exit /b 1
)
echo [OK] Upload succeeded.
