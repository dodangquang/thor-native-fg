# 🚀 Quick Start - FG & AA Improvements

## ✅ Xác nhận thay đổi

```powershell
# Chạy verification script
.\verify-improvements.ps1
```

**Expected:** 19/19 checks PASS

---

## 🔨 Build APK

### Yêu cầu
- Android SDK với NDK 27.3.13750724
- Java JDK 17+
- Build tools 35.0.0+

### Build lệnh

```powershell
# Auto-detect SDK
.\build.ps1

# Hoặc chỉ định SDK path
.\build.ps1 -Sdk "C:\Android\Sdk"
```

**Output:** `build/ThorNativeFG-debug.apk`

---

## 📱 Cài đặt & Test

### 1. Backup hiện tại
```bash
adb pull /data/app/com.thor.nativefg-*/base.apk backup-current.apk
```

### 2. Cài APK mới
```bash
adb install -r build/ThorNativeFG-debug.apk
```

### 3. Force-stop apps đang chạy
```bash
adb shell am force-stop <game-package>
```

### 4. Test scenarios

#### A. Flicker Test (FG Stability)
1. Mở game với FG enabled
2. Test các tình huống:
   - ✓ Fast camera panning (360° xoay nhanh)
   - ✓ Character chạy/nhảy với camera theo
   - ✓ Racing game ở tốc độ cao
3. Quan sát: Giảm nhấp nháy so với trước

#### B. Scene Cut Test
1. Test chuyển cảnh:
   - ✓ Loading screen → gameplay (phải detect)
   - ✓ Fade in/out transitions (phải detect)
   - ✓ Quick cuts trong cutscene
2. Xác nhận: Vẫn fallback về real frames đúng

#### C. FXAA Quality Test
1. Enable FXAA trong settings
2. Kiểm tra:
   - ✓ Text trong menu - phải sắc nét
   - ✓ HUD elements - không bị blur
   - ✓ Chữ nhỏ - phải đọc được
   - ✓ Geometry edges - smooth hơn
3. So sánh FXAA ON vs OFF

---

## 📊 Thu thập Metrics

### Quality Stats
```bash
# Monitor FG decisions
adb logcat -s ThorFG:V | grep -E 'allow|reliable|valid'

# Expected improvements:
# - allow=1 nhiều hơn trong panning scenes
# - reliable% cao hơn (≥80% common)
# - valid% ổn định
```

### Performance
```bash
# GPU timing (should be unchanged)
adb shell dumpsys gfxinfo <package> framestats

# Thermal
adb shell cat /sys/class/thermal/thermal_zone*/temp
```

---

## 🐛 Troubleshooting

### "Still flickering"
1. Check FG actually enabled:
   ```bash
   adb shell getprop persist.thor.fg.enable
   ```
2. Check game using layer:
   ```bash
   adb logcat -s ThorFG:V | grep "blit"
   ```
3. Collect logs + screen recording

### "FXAA blurring text"
1. Verify new APK installed:
   ```bash
   adb shell dumpsys package com.thor.nativefg | grep versionCode
   ```
2. Toggle FXAA off/on in app
3. Restart game completely

### "No improvement"
1. Confirm shader changes loaded:
   ```bash
   adb logcat -s ThorFG:V | grep "pipeline"
   ```
2. Clear app data and reconfigure
3. Compare with APK hash from build log

---

## 📈 Success Criteria

| Metric | Target |
|--------|--------|
| Flicker reduction | ✓ Noticeable in fast pans |
| FG allow rate | ✓ Higher (≥5% increase) |
| Text clarity (FXAA on) | ✓ Same as FXAA off |
| Scene cut detection | ✓ Still 100% accurate |
| Performance | ✓ No regression |

---

## 🔄 Rollback

Nếu có vấn đề:

```bash
# Cài lại version trước
adb install -r ThorNativeFG-FG-stability-test.apk

# Verify hash
certutil -hashfile ThorNativeFG-FG-stability-test.apk SHA256
# Should be: 973A5B1276BED0FB435E7F5EAE3ABB4CEA507436954F93B97FC595FCDD286475
```

---

## 📝 Báo cáo Issues

Nếu tìm thấy vấn đề, cung cấp:

1. **Logs**
   ```bash
   adb logcat -s ThorFG:V > issue-log.txt
   ```

2. **Screen recording** của vấn đề

3. **Info**
   - Game/app name và package
   - Settings: SR model, FG on/off, FXAA on/off
   - Thor model và Android version
   - Exact scenario tái hiện

4. **Comparison**
   - Issue có xảy ra với APK cũ không?
   - FXAA on vs off có khác biệt không?

---

## 📚 Tài liệu chi tiết

- [IMPROVEMENTS-SUMMARY-VI.txt](IMPROVEMENTS-SUMMARY-VI.txt) - Chi tiết kỹ thuật
- [CHANGELOG-FG-AA-IMPROVEMENTS.md](CHANGELOG-FG-AA-IMPROVEMENTS.md) - Changelog đầy đủ
- [verify-improvements.ps1](verify-improvements.ps1) - Verification script

---

## ⚡ Quick Commands Reference

```bash
# Full workflow
.\verify-improvements.ps1           # Verify changes
.\build.ps1                         # Build APK
adb install -r build/ThorNativeFG-debug.apk  # Install
adb shell am force-stop <game>      # Restart game
adb logcat -s ThorFG:V              # Monitor

# One-liner test
adb install -r build/ThorNativeFG-debug.apk && adb shell am force-stop <game> && adb logcat -s ThorFG:V
```

---

**Version:** Post-improvements (Oct 25, 2026)  
**Changes:** Quality adaptive thresholds + Multi-stage blending + Selective FXAA + Chroma flow  
**Status:** Ready for testing
