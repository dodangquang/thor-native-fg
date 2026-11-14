# 🔧 Fix Thor Native FG - Lag & Flickering Issues

## 🎯 Problem Analysis

Bạn gặp:
- 🟡 **Chậm (Lag)** - Game bị giảm FPS hoặc frame bị delay
- 🟡 **Flick (Flickering)** - Hình ảnh không ổn định, bị jump/stutter

**Nguyên nhân thường gặp:**

| Triệu Chứng | Nguyên Nhân | Fix |
|------------|-----------|-----|
| FPS từ 60 → 30-45 | TAA/CAS overhead quá lớn | Giảm CAS sharpness hoặc disable TAA |
| Hình ảnh flick/stutter | TAA blend factor sai | Tăng taaBlendFactor từ 0.08 → 0.12 |
| Lag + bộ nhớ error | GPU memory full | Disable CAS hoặc reduce resolution |
| Lag tăng dần | CPU bottleneck | Disable TAA, keep CAS |

---

## ⚡ Quick Fix (5 phút)

### **Option 1: Disable CAS (Reduce Overhead)**

Nếu **main problem là chậm**, CAS có overhead ~0.28ms. Disable nó:

Mở file: `native/engine.cpp`

Tìm dòng:
```cpp
bool enableCAS = true;
```

Sửa thành:
```cpp
bool enableCAS = false;  // Disable CAS to reduce overhead
```

**Effect:**
- ✅ Giảm overhead ~0.3ms → FPS tăng
- ❌ Hình ảnh sẽ soft hơn (vì mất CAS sharpening)
- ✅ Neural upscale + TAA vẫn hoạt động

---

### **Option 2: Adjust TAA Blend (Fix Flickering)**

Nếu **main problem là flick**, nguyên nhân là TAA blend factor:

Mở file: `native/engine.cpp`

Tìm dòng:
```cpp
float taaBlendFactor = 0.08f;
```

**Nếu thấy flickering/stutter:**
```cpp
float taaBlendFactor = 0.12f;  // Increase blending (more stable)
```

**Nếu thấy ghosting (trailing):**
```cpp
float taaBlendFactor = 0.05f;  // Decrease blending (more responsive)
```

**Effect:**
- taaBlendFactor cao (0.10-0.15) = stable nhưng có thể ghosting
- taaBlendFactor thấp (0.03-0.06) = responsive nhưng có thể flicker

---

### **Option 3: Disable TAA Entirely**

Nếu **TAA cả lỗi hoặc overhead quá lớn:**

Mở file: `native/engine.cpp`

Tìm dòng:
```cpp
bool enableTAA = true;
```

Sửa thành:
```cpp
bool enableTAA = false;  // Disable TAA - use spatial AA only
```

**Effect:**
- ✅ Giảm overhead ~0.35ms
- ✅ GPU load giảm
- ❌ Mất temporal anti-aliasing (FXAA vẫn hoạt động)
- ℹ️ Vẫn có neural upscale + CAS

---

## 🎯 Full Troubleshooting Guide

### **Diagnosis: Làm Thế Nào Biết Problem Là Gì?**

1. **Download script diagnostic**:
```bash
# Chạy trong desktop PowerShell
cd C:\Users\dodan\thor-native-fg
.\DIAGNOSE-DLSS.bat
```

2. **Script sẽ**:
   - Collect logs từ device (30 giây)
   - Analyze frame times, FPS, overhead
   - Suggest fixes

3. **Xem output**:
   - `diagnostic_logs.txt` - Full logs
   - `diagnostic_analysis.txt` - Filtered issues

---

### **Scenario 1: FPS Giảm (60 → 30-45)**

**Diagnosis**: 
- Check: `diagnostic_logs.txt` có "FPS: 30" hoặc "FPS: 45"?
- Check: overhead cao? (TAA 0.35ms + CAS 0.28ms + neural 1.2ms = 1.83ms vs ideal 16.67ms budget)

**Root Cause Possibilities**:
1. ✅ **CAS quá heavy** (sharpness quá cao)
2. ✅ **Neural upscaling bottleneck** (resolution mismatch)
3. ✅ **TAA GPU stall** (history buffer locking)
4. ❓ Game rendering quá complex

**Fixes (theo priority)**:

**Fix 1A - Disable CAS** (likely culprit):
```cpp
bool enableCAS = false;
// Rebuild: python toolchain/build.py --sdk toolchain/sdk
```

**Fix 1B - Reduce CAS sharpness**:
```cpp
float casSharpness = 0.4f;  // Reduce from 0.6
```

**Fix 1C - Disable TAA**:
```cpp
bool enableTAA = false;
```

**Fix 1D - Check neural model** (in engine.cpp):
```cpp
spatialModel = SpatialModel::Sgsr1;  // Try simpler upscaler than Neural2D
```

---

### **Scenario 2: Flickering/Stutter**

**Diagnosis**:
- Visual: Hình ảnh bị jump, không smooth
- Check: `diagnostic_logs.txt` cho "flick" hoặc "inconsistent frame time"

**Root Cause Possibilities**:
1. ✅ **TAA blend factor sai** (mismatch temporal accumulation)
2. ✅ **History frame mismatch** (scene cut detection fail)
3. ❓ Frame generation timing issue

**Fixes (theo priority)**:

**Fix 2A - Adjust TAA blend**:
```cpp
// Current: 0.08f
// Try: 0.10f hoặc 0.12f (more stable)
float taaBlendFactor = 0.12f;
```

**Fix 2B - Disable TAA temporal part** (keep spatial):
```cpp
bool enableTAA = false;  // Use FXAA spatial AA only
```

**Fix 2C - Check scene cut detection** (quality.comp):
- Scene cut detection có thể sai → TAA bị reset
- Check: `shaders/quality.comp` line 14-33
- Adjust thresholds nếu cần

---

### **Scenario 3: Both Lag + Flickering**

Nếu **cả 2 vấn đề cùng xảy ra**, có thể là:

**Possible**:
- TAA implementation bug (ghosting + overhead)
- CAS overhead + TAA instability
- Memory fragmentation (GPU buffer issue)

**Nuclear Option - Rollback**:
```powershell
# Quay lại version cũ (chắc chắn hoạt động)
adb install -r ThorNativeFG-FG-stability-test.apk
```

**Then rebuild**:
```powershell
# Build version đơn giản hơn (no TAA/CAS)
# Edit native/engine.cpp:
#   enableTAA = false;
#   enableCAS = false;

python toolchain/build.py --sdk toolchain/sdk
adb install -r ThorNativeFG-debug.apk
```

---

## 🔨 Step-by-Step Fix

### **Step 1: Identify Main Problem**

```bash
# Chạy diagnostic
cd C:\Users\dodan\thor-native-fg
.\DIAGNOSE-DLSS.bat

# Read logs:
# - Thấy "FPS: 30"? → Lag issue
# - Thấy "flick" hoặc "inconsistent"? → Flicker issue
# - Thấy "memory error"? → Memory issue
```

### **Step 2: Apply Corresponding Fix**

**Lag (FPS low)**:
```cpp
// Edit native/engine.cpp
bool enableCAS = false;  // Or try casSharpness = 0.4f
```

**Flickering**:
```cpp
// Edit native/engine.cpp
float taaBlendFactor = 0.12f;  // Or try 0.10f, 0.15f
```

**Both issues**:
```cpp
// Edit native/engine.cpp
bool enableTAA = false;
bool enableCAS = false;
// Or rollback to stable version
```

### **Step 3: Rebuild APK**

```powershell
cd C:\Users\dodan\thor-native-fg

# Rebuild
python toolchain/build.py --sdk toolchain/sdk

# Install
adb install -r ThorNativeFG-debug.apk
```

### **Step 4: Test**

- Play game for 2-3 minutes
- Check:
  - FPS stable @ 60?
  - Hình ảnh smooth?
  - No flicker?

### **Step 5: Fine-tune (Optional)**

Nếu better nhưng muốn optimize hơn:

```cpp
// If lag fixed but want more sharpness:
float casSharpness = 0.5f;  // Từ 0.4

// If flicker fixed but want more temporal stability:
float taaBlendFactor = 0.14f;  // Từ 0.12
```

---

## 📊 Parameter Tuning Reference

### **TAA Blend Factor** (default 0.08f)

| Value | Stability | Ghosting | Use Case |
|-------|-----------|----------|----------|
| 0.03 | ⚠️ Unstable | ✅ None | Ultra-responsive (not recommended) |
| 0.05 | 🟡 OK | ✅ None | Very responsive |
| 0.08 | ✅ Good | 🟡 Slight | **Balanced (default)** |
| 0.10 | ✅✅ Very stable | 🟡 Noticeable | High motion content |
| 0.12 | ✅✅✅ Very stable | ⚠️ Some ghosting | For flickering issues |
| 0.15 | ⚠️ Over-stable | ⚠️ High | Not recommended |

**Recommendation**:
- 60fps: 0.08f (default)
- Fast-paced games: 0.05-0.06f
- Flickering issues: 0.10-0.12f
- Never use > 0.15f

---

### **CAS Sharpness** (default 0.6f)

| Value | Sharpness | Overhead | Use Case |
|-------|-----------|----------|----------|
| 0.0 | ❌ None | ✅ None | No sharpening (disable CAS) |
| 0.3 | 🟡 Light | ✅ Light | Soft look |
| 0.5 | ✅ Moderate | ✅ Moderate | Film-like |
| **0.6** | ✅ Good | 🟡 Moderate | **Balanced (default)** |
| 0.7 | ✅✅ Sharp | ⚠️ Heavier | Game graphics |
| 0.8+ | ⚠️ Over-sharp | ⚠️ Heavy | Risk of halos |

**Recommendation**:
- If lag: disable (0.0) or reduce to 0.4f
- If performance OK: keep 0.6f
- If want sharpness: 0.7f (watch for halos)

---

## 🔄 If Nothing Works: Rollback

```powershell
# Quay lại version cũ
adb install -r ThorNativeFG-FG-stability-test.apk

# Or completely disable Frame Gen
adb uninstall com.example.thornativefg
```

---

## 📈 Expected Performance

### **Normal Behavior** (should see):
- FPS: 60 (stable)
- Frame time: ~16.6ms
- No flicker
- Smooth camera pans

### **With DLSS-like** (our setup):
- TAA: +0.35ms
- Neural upscale: +1.2ms (depends on resolution)
- CAS: +0.28ms
- FXAA: +0.18ms
- **Total: ~2.0ms** out of 16.6ms budget = excellent headroom

### **If You See**:
- FPS: 30-45 → Too much overhead, disable CAS
- Flicker/stutter → Adjust TAA blend
- Smooth but soft → Increase CAS sharpness
- Smooth and sharp → Good! (you're done)

---

## 🆘 Common Issues & Solutions

| Issue | Symptom | Fix |
|-------|---------|-----|
| High overhead | FPS 30-45 | Disable CAS: `enableCAS = false` |
| Flickering | Jitter/stutter | Increase TAA: `taaBlendFactor = 0.12f` |
| Ghosting | Trailing artifacts | Decrease TAA: `taaBlendFactor = 0.05f` |
| Blurry | Soft image | Increase CAS: `casSharpness = 0.7f` |
| Memory crash | App close suddenly | Disable TAA: `enableTAA = false` |
| Scene flicker | Black frames | Check quality.comp thresholds |

---

## 📝 Checklist

- [ ] Downloaded DIAGNOSE-DLSS.bat
- [ ] Run diagnostic to identify main issue
- [ ] Applied fix based on symptom
- [ ] Rebuilt APK: `python toolchain/build.py --sdk toolchain/sdk`
- [ ] Installed new APK: `adb install -r ThorNativeFG-debug.apk`
- [ ] Tested in game for 2-3 minutes
- [ ] Confirmed: FPS stable, no flicker, image quality acceptable
- [ ] Fine-tuned parameters if needed

---

## 🎯 Next Steps

1. **If FIX WORKED**: You're done! Enjoy DLSS-like quality!
2. **If PARTIALLY worked**: Fine-tune with parameter adjustments
3. **If NOTHING worked**: Rollback to stable version, let me know exact symptoms

---

**Questions?** Check logcat:
```bash
adb logcat -s ThorNativeFG:V
```

Monitor for errors, overhead times, FPS reports.
