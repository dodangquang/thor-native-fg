# Thor Native FG - Quick DLSS Fix Script
# Apply common fixes without manual code editing

param(
    [string]$Fix = ""
)

function Show-Menu {
    Write-Host "╔════════════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
    Write-Host "║     THOR NATIVE FG - QUICK DLSS FIX                            ║" -ForegroundColor Cyan
    Write-Host "╚════════════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Your issue:" -ForegroundColor Yellow
    Write-Host "  Game is LAG + FLICK" -ForegroundColor Red
    Write-Host ""
    Write-Host "Choose fix:" -ForegroundColor Green
    Write-Host "  1 - Disable CAS (reduce overhead, fix LAG)" -ForegroundColor White
    Write-Host "  2 - Reduce CAS sharpness (keep CAS but lighter)" -ForegroundColor White
    Write-Host "  3 - Adjust TAA blend (fix FLICK)" -ForegroundColor White
    Write-Host "  4 - Disable TAA (use spatial AA only)" -ForegroundColor White
    Write-Host "  5 - Disable BOTH TAA+CAS (maximum stability)" -ForegroundColor White
    Write-Host "  6 - Rollback to stable version" -ForegroundColor White
    Write-Host "  0 - Exit" -ForegroundColor White
    Write-Host ""
}

function Apply-Fix {
    param([int]$FixNumber)
    
    $engineFile = "native/engine.cpp"
    
    Write-Host ""
    Write-Host "Applying fix #$FixNumber..." -ForegroundColor Green
    Write-Host ""
    
    switch($FixNumber) {
        1 {
            # Disable CAS
            Write-Host "Fix: Disabling CAS (reduce overhead)" -ForegroundColor Yellow
            $content = Get-Content $engineFile -Raw
            $content = $content -replace 'bool enableCAS = true;', 'bool enableCAS = false;  // Disabled to fix LAG'
            Set-Content $engineFile $content
            Write-Host "✓ CAS disabled in native/engine.cpp" -ForegroundColor Green
            Write-Host ""
            Write-Host "Effect: Reduces overhead by ~0.28ms, might lose sharpness" -ForegroundColor Cyan
        }
        2 {
            # Reduce CAS sharpness
            Write-Host "Fix: Reducing CAS sharpness to 0.4f" -ForegroundColor Yellow
            $content = Get-Content $engineFile -Raw
            $content = $content -replace 'float casSharpness = 0\.6f;', 'float casSharpness = 0.4f;  // Reduced to fix LAG'
            Set-Content $engineFile $content
            Write-Host "✓ CAS sharpness reduced in native/engine.cpp" -ForegroundColor Green
            Write-Host ""
            Write-Host "Effect: Reduces overhead, keeps some sharpness" -ForegroundColor Cyan
        }
        3 {
            # Adjust TAA blend
            Write-Host "Fix: Increasing TAA blend factor to 0.12f (fix FLICK)" -ForegroundColor Yellow
            $content = Get-Content $engineFile -Raw
            $content = $content -replace 'float taaBlendFactor = 0\.08f;', 'float taaBlendFactor = 0.12f;  // Increased to fix FLICK'
            Set-Content $engineFile $content
            Write-Host "✓ TAA blend factor increased in native/engine.cpp" -ForegroundColor Green
            Write-Host ""
            Write-Host "Effect: More stable, might see slight ghosting" -ForegroundColor Cyan
        }
        4 {
            # Disable TAA
            Write-Host "Fix: Disabling TAA (use spatial AA only)" -ForegroundColor Yellow
            $content = Get-Content $engineFile -Raw
            $content = $content -replace 'bool enableTAA = true;', 'bool enableTAA = false;  // Disabled to fix LAG/FLICK'
            Set-Content $engineFile $content
            Write-Host "✓ TAA disabled in native/engine.cpp" -ForegroundColor Green
            Write-Host ""
            Write-Host "Effect: Reduces overhead, loses temporal AA benefit" -ForegroundColor Cyan
        }
        5 {
            # Disable both
            Write-Host "Fix: Disabling BOTH TAA and CAS (maximum stability)" -ForegroundColor Yellow
            $content = Get-Content $engineFile -Raw
            $content = $content -replace 'bool enableTAA = true;', 'bool enableTAA = false;  // Disabled - nuclear option'
            $content = $content -replace 'bool enableCAS = true;', 'bool enableCAS = false;  // Disabled - nuclear option'
            Set-Content $engineFile $content
            Write-Host "✓ Both TAA and CAS disabled in native/engine.cpp" -ForegroundColor Green
            Write-Host ""
            Write-Host "Effect: Maximum stability, but basic AA quality only" -ForegroundColor Cyan
        }
        6 {
            # Rollback
            Write-Host "Fix: Rolling back to stable version" -ForegroundColor Yellow
            Write-Host "Running: adb install -r ThorNativeFG-FG-stability-test.apk" -ForegroundColor Cyan
            & "toolchain\platform-tools\adb.exe" install -r ThorNativeFG-FG-stability-test.apk
            Write-Host ""
            Write-Host "✓ Rolled back to stable version" -ForegroundColor Green
            return
        }
        default {
            Write-Host "Invalid choice" -ForegroundColor Red
            return
        }
    }
    
    Write-Host ""
    Write-Host "════════════════════════════════════════════════════════════════" -ForegroundColor Cyan
    Write-Host "NEXT STEPS:" -ForegroundColor Yellow
    Write-Host "════════════════════════════════════════════════════════════════" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "1. Rebuild APK:" -ForegroundColor Green
    Write-Host "   python toolchain\build.py --sdk toolchain\sdk" -ForegroundColor White
    Write-Host ""
    Write-Host "2. Install to device:" -ForegroundColor Green
    Write-Host "   adb install -r ThorNativeFG-debug.apk" -ForegroundColor White
    Write-Host ""
    Write-Host "3. Test in game:" -ForegroundColor Green
    Write-Host "   Play for 2-3 minutes, check FPS and flicker" -ForegroundColor White
    Write-Host ""
    Write-Host "4. Monitor logs (if still having issues):" -ForegroundColor Green
    Write-Host "   adb logcat -s ThorNativeFG:V" -ForegroundColor White
    Write-Host ""
}

# Main
if ($Fix -eq "") {
    Show-Menu
    $choice = Read-Host "Enter choice"
} else {
    $choice = $Fix
}

if ($choice -eq "0") {
    Write-Host "Exiting..." -ForegroundColor Yellow
    exit 0
}

Apply-Fix $choice

Write-Host ""
Write-Host "✓ Ready to rebuild! Run the commands above to complete." -ForegroundColor Green
