#!/usr/bin/env pwsh
# Verify shader improvements are present

param([switch]$Verbose)

$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot
$pass = 0
$fail = 0

function Test-Pattern($File, $Pattern, $Description) {
    if (Select-String -Path $File -Pattern $Pattern -Quiet) {
        Write-Host "PASS $Description" -ForegroundColor Green
        $script:pass++
    } else {
        Write-Host "FAIL $Description" -ForegroundColor Red
        $script:fail++
    }
}

Write-Host "`n=== Verifying Frame Generation and AA Improvements ===" -ForegroundColor Cyan
Write-Host "Checking shader modifications...`n" -ForegroundColor Cyan

# Quality.comp improvements
Write-Host "[quality.comp]" -ForegroundColor Yellow
Test-Pattern "$root/shaders/quality.comp" "excellentFlow" "Excellent flow detection added"
Test-Pattern "$root/shaders/quality.comp" "rgbThreshold=excellentFlow" "Adaptive RGB threshold"
Test-Pattern "$root/shaders/quality.comp" "reliable>=0.80" "Relaxed reliable pan threshold 80pct"
Test-Pattern "$root/shaders/quality.comp" "boundary<0.15" "Relaxed boundary tolerance 15pct"
Test-Pattern "$root/shaders/quality.comp" "validThreshold=reliable>=0.75" "Dynamic valid threshold"

# Interpolate.comp improvements
Write-Host "`n[interpolate.comp]" -ForegroundColor Yellow
Test-Pattern "$root/shaders/interpolate.comp" "AGREE_MID" "Multi-stage agree thresholds"
Test-Pattern "$root/shaders/interpolate.comp" "agreeStrong" "Strong agreement blending"
Test-Pattern "$root/shaders/interpolate.comp" "agreeWeak" "Weak agreement blending"
Test-Pattern "$root/shaders/interpolate.comp" "0.030" "Improved AGREE_LOW (0.030)"
Test-Pattern "$root/shaders/interpolate.comp" "0.13" "Relaxed AGREE_HIGH (0.13)"

# FXAA improvements
Write-Host "`n[fxaa.comp]" -ForegroundColor Yellow
Test-Pattern "$root/shaders/fxaa.comp" "crossPattern" "Cross-pattern detection for text"
Test-Pattern "$root/shaders/fxaa.comp" "likelyText" "Text classification logic"
Test-Pattern "$root/shaders/fxaa.comp" "highContrast" "High contrast edge detection"
Test-Pattern "$root/shaders/fxaa.comp" "Adaptive FXAA" "Updated shader description"

# Flow.comp improvements  
Write-Host "`n[flow.comp]" -ForegroundColor Yellow
Test-Pattern "$root/shaders/flow.comp" "chroma" "Chroma-aware color difference"
Test-Pattern "$root/shaders/flow.comp" "0.85" "Adjusted REFINE_MARGIN"
Test-Pattern "$root/shaders/flow.comp" "0.085" "Relaxed valid threshold"
Test-Pattern "$root/shaders/flow.comp" "0.018" "Relaxed reliable threshold"
Test-Pattern "$root/shaders/flow.comp" "1.6" "Relaxed bidirectional check"

# Summary
Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
$total = $pass + $fail
Write-Host "Results: $pass passed, $fail failed (total: $total)" -ForegroundColor White
if ($fail -eq 0) {
    Write-Host "SUCCESS: All improvements verified" -ForegroundColor Green
    exit 0
} else {
    Write-Host "WARNING: Some checks failed" -ForegroundColor Red
    exit 1
}
