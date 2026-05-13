@echo off
:: build.bat — compile only, no upload
cd /d "%~dp0.."
echo [BUILD] Compiling firmware...
pio run
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [FAIL] Build failed. Check src\main.cpp for errors above.
    exit /b 1
)
echo [OK] Build succeeded.
