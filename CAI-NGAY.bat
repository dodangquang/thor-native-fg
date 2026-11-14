@echo off
REM Thor Native FG - Quick Install Script
REM Run this from desktop Windows (NOT in CoWork sandbox)

echo ===================================================================
echo   THOR NATIVE FG - CAI DAT NHANH
echo ===================================================================
echo.

cd /d "%~dp0"

echo [1/3] Checking ADB connection...
toolchain\platform-tools\adb.exe devices
echo.

echo [2/3] Installing APK to AYN Thor...
echo Installing: ThorNativeFG-FG-stability-test.apk
toolchain\platform-tools\adb.exe install -r ThorNativeFG-FG-stability-test.apk
echo.

echo [3/3] Verifying installation...
toolchain\platform-tools\adb.exe shell pm list packages | findstr thor
echo.

echo ===================================================================
echo   HOAN THANH!
echo ===================================================================
echo.
echo Test tren AYN Thor:
echo   1. Mo game voi Frame Generation enabled
echo   2. Quan sat: giam flicker, smooth hon, text ro
echo.
echo Monitor logs:
echo   adb logcat ^| findstr ThorNativeFG
echo.
pause
