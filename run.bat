@echo off
setlocal enabledelayedexpansion
:: run.bat — Central launcher. Double-click this to get started.
cd /d "%~dp0"

:menu
cls
echo.
echo  ==========================================
echo    WS_PIO  ^|  Arduino Nano ESP32
echo  ==========================================

:: Show live port status
set PORT_STATUS=NOT FOUND
python -c "import serial.tools.list_ports; ports=[p.device for p in serial.tools.list_ports.comports()]; print('COM11 connected') if 'COM11' in ports else print('COM11 not found')" 2>nul > .port_check.tmp
set /p PORT_STATUS=<.port_check.tmp
del .port_check.tmp 2>nul
echo    Board: %PORT_STATUS%
echo.
echo    1.  Build only
echo    2.  Upload to board
echo    3.  Monitor serial output
echo    4.  Upload + Monitor  ^(most common^)
echo    5.  Exit
echo.
set /p choice="  Select [1-5]: "

if "%choice%"=="1" ( call scripts\build.bat & pause & goto menu )
if "%choice%"=="2" ( call scripts\upload.bat & pause & goto menu )
if "%choice%"=="3" ( call scripts\monitor.bat & goto menu )
if "%choice%"=="4" ( call scripts\upload_and_monitor.bat & goto menu )
if "%choice%"=="5" ( exit /b 0 )

echo  Invalid choice.
pause
goto menu
