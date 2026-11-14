@echo off
REM Thor Native FG - DLSS-like Diagnostic Tool
REM Collects logs to identify performance issues

setlocal enabledelayedexpansion
cd /d "%~dp0"

echo ===================================================================
echo   THOR NATIVE FG - DLSS DIAGNOSTIC TOOL
echo ===================================================================
echo.
echo This script will:
echo   1. Check ADB connection
echo   2. Collect ThorNativeFG logs
echo   3. Monitor FPS and frame times
echo   4. Identify bottleneck
echo.

REM Check ADB
echo [1/3] Checking ADB connection...
toolchain\platform-tools\adb.exe devices
echo.

REM Check if device is connected
for /f "tokens=2" %%a in ('toolchain\platform-tools\adb.exe devices ^| findstr device ^| findstr -v devices') do set DEVICE=%%a
if "%DEVICE%"=="" (
    echo ERROR: No device found! 
    echo Make sure AYN Thor is connected via USB with debugging enabled.
    pause
    exit /b 1
)

echo Device found: %DEVICE%
echo.

REM Clear previous logs
echo [2/3] Clearing previous logs...
toolchain\platform-tools\adb.exe logcat -c
echo Logs cleared. Start playing game now...
echo.
timeout /t 5

REM Start collecting logs
echo [3/3] Collecting logs (30 seconds)...
echo Press Ctrl+C to stop early
echo.
echo Collecting performance metrics...
toolchain\platform-tools\adb.exe logcat -s ThorNativeFG:V > diagnostic_logs.txt 2>&1
timeout /t 30

echo.
echo ===================================================================
echo   LOGS SAVED: diagnostic_logs.txt
echo ===================================================================
echo.
echo Analyzing logs...
echo.

REM Analyze logs
findstr /c:"FPS" diagnostic_logs.txt > diagnostic_analysis.txt
findstr /c:"ms" diagnostic_logs.txt >> diagnostic_analysis.txt
findstr /c:"overhead" diagnostic_logs.txt >> diagnostic_analysis.txt
findstr /c:"TAA" diagnostic_logs.txt >> diagnostic_analysis.txt
findstr /c:"CAS" diagnostic_logs.txt >> diagnostic_analysis.txt
findstr /c:"error" diagnostic_logs.txt >> diagnostic_analysis.txt
findstr /c:"slow" diagnostic_logs.txt >> diagnostic_analysis.txt

type diagnostic_analysis.txt
echo.

REM Provide quick diagnostics
echo ===================================================================
echo   QUICK DIAGNOSIS
echo ===================================================================
echo.
echo Check the logs above for:
echo.
echo 1. IF YOU SEE "FPS: 30" or "FPS: 20":
echo    Problem: GPU overloaded
echo    Fix: Disable CAS or TAA in engine.cpp
echo.
echo 2. IF YOU SEE "overhead: 2.5ms+" or "2000ms+":
echo    Problem: TAA/CAS consuming too much time
echo    Fix: Reduce casSharpness from 0.6 to 0.4
echo         OR disable TAA (enableTAA = false)
echo.
echo 3. IF YOU SEE flicker patterns:
echo    Problem: TAA blend factor mismatch
echo    Fix: Increase taaBlendFactor from 0.08 to 0.12
echo.
echo 4. IF YOU SEE memory allocation errors:
echo    Problem: Image buffers too large
echo    Fix: Reduce resolution or disable features
echo.

echo ===================================================================
echo   NEXT STEPS
echo ===================================================================
echo.
echo Option 1 - QUICK FIX (disable heavy features):
echo   Rollback to stable version:
echo   adb install -r ThorNativeFG-FG-stability-test.apk
echo.
echo Option 2 - TUNE PARAMETERS:
echo   Edit native/engine.cpp:
echo   - enableTAA = false;        (disable TAA if overhead issue)
echo   - enableCAS = false;        (disable CAS if overhead issue)
echo   - casSharpness = 0.4f;      (reduce from 0.6 if heavy)
echo   - taaBlendFactor = 0.12f;   (increase from 0.08 if flickering)
echo.
echo Option 3 - BUILD WITH DIFFERENT CONFIG:
echo   python toolchain/build.py --sdk toolchain/sdk --lite
echo.

echo ===================================================================
echo Files created:
echo   - diagnostic_logs.txt (full logs)
echo   - diagnostic_analysis.txt (filtered logs)
echo ===================================================================
echo.

pause
