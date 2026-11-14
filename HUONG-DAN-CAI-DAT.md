# 📱 Hướng Dẫn Cài Đặt Thor Native FG - DLSS-like Quality

## ⚠️ QUAN TRỌNG: Tại Sao Không Thể Build Trong Sandbox?

Sandbox CoWork **CHẶN** việc thực thi:
- ❌ NDK clang++ (C++ compiler) - "Permission denied"  
- ❌ adb child processes - Không thể spawn adb để install APK
- ❌ Java .bat/.cmd wrappers - Execution policy

**Giải pháp**: Build APK trên máy Windows desktop (ngoài sandbox), rồi cài thủ công qua adb.

---

## 🎯 Lộ Trình 1: Cài Bản Có Sẵn (NẾU CÓ APK CŨ)

Nếu bạn đã build APK trước đó với improvements (quality, interpolate, fxaa, flow):

### Bước 1: Tìm APK
```bash
# Kiểm tra có APK nào không
ls -lh *.apk
ls -lh build/*.apk
```

### Bước 2: Cài Qua ADB (Desktop/PowerShell)
```powershell
# Mở PowerShell NGOÀI CoWork
cd C:\Users\dodan\thor-native-fg

# Kết nối AYN Thor qua USB, bật USB debugging
toolchain\platform-tools\adb.exe devices

# Cài APK
toolchain\platform-tools\adb.exe install -r ThorNativeFG-FG-stability-test.apk
```

---

## 🚀 Lộ Trình 2: Build APK Mới Với DLSS-like Quality (30 phút)

### **📋 Checklist Trước Khi Build**

✅ **Đã hoàn thành**:
- Shaders compiled: `build/*.spv` (19 files)
- Headers generated: `shaders/*.spv.hpp` (19 files)
- Documentation ready

⚠️ **Cần làm**:
- Tích hợp C++ code (engine.hpp, engine.cpp)
- Build APK outside sandbox
- Install qua adb

---

### **Bước 1: Tích Hợp C++ Code** (5-10 phút)

#### A. Mở `native/engine.hpp` và thêm vào struct `Engine`:

Tìm dòng này:
```cpp
Pipeline aaPipe;
VkDescriptorPool aaDescriptors{};
VkDescriptorSet aaSet{};
```

Thêm NGAY SAU đó:
```cpp
// DLSS-like quality additions
Pipeline taaPipe;         // Temporal Anti-Aliasing
Pipeline casPipe;         // Contrast Adaptive Sharpening
Image taaHistory;         // TAA history buffer
Image casOutput;          // CAS output buffer
VkDescriptorSet taaSet{}; 
VkDescriptorSet casSet{};
bool enableTAA = true;
bool enableCAS = true;
float casSharpness = 0.6f;
```

#### B. Mở `native/engine.cpp` và thêm shader includes:

Tìm phần includes (gần đầu file):
```cpp
#include "shaders/fxaa.spv.hpp"
```

Thêm NGAY SAU:
```cpp
#include "shaders/taa.spv.hpp"
#include "shaders/cas.spv.hpp"
```

#### C. Thêm Push Constant Structures

Thêm vào đầu file hoặc sau các struct khác:
```cpp
struct TAAParams {
    int32_t width;
    int32_t height;
    float jitterX;
    float jitterY;
    float blendFactor;
};

struct CASParams {
    int32_t width;
    int32_t height;
    float sharpness;
    int32_t preserveText;
};
```

#### D. Initialize TAA & CAS trong `Engine::init()` hoặc `initSpatial()`

**CHÚ Ý**: Phần này phức tạp hơn. Xem file `DLSS-LIKE-UPGRADE-VI.md` section "Bước 3" để có code đầy đủ.

**TÓM TẮT cần làm**:
- Tạo taaHistory image buffer
- Tạo taaPipe với bindings [sampler, sampler, sampler, storage]
- Tạo casOutput image buffer  
- Tạo casPipe với bindings [sampler, storage]
- Allocate descriptor sets

**Nếu không chắc**: Copy code từ `DLSS-LIKE-UPGRADE-VI.md` lines 180-250.

---

### **Bước 2: Build APK** (10-15 phút)

#### A. Mở PowerShell Desktop (NGOÀI CoWork)

```powershell
# Chuyển đến thư mục project
cd C:\Users\dodan\thor-native-fg

# Kiểm tra files
ls build\*.spv  # Should show 19 .spv files
ls shaders\*.spv.hpp  # Should show 19 .spv.hpp files
```

#### B. Build APK bằng Python script

```powershell
# Option 1: Dùng Python (recommended)
python toolchain\build.py --sdk toolchain\sdk

# Option 2: Dùng PowerShell (nếu Python không work)
powershell -ExecutionPolicy Bypass -File toolchain\build.ps1 -Sdk toolchain\sdk
```

**Expected output**:
```
Compiling C++ native library...
Linking libthornativefg.so...
Packaging APK...
Signing APK...
✓ ThorNativeFG-DLSS-Quality.apk created (5-8MB)
```

#### C. Troubleshooting Build Errors

| Error | Fix |
|-------|-----|
| `clang++: not found` | Check NDK path: `toolchain\sdk\ndk\27.3.13750724` |
| `taa.spv.hpp: No such file` | Run: `python toolchain\embed_shaders.py` |
| `Java not found` | Install JDK 11+ hoặc set JAVA_HOME |
| `Permission denied` | Run PowerShell as Administrator |

---

### **Bước 3: Cài Đặt APK Lên AYN Thor** (2-5 phút)

#### A. Kết nối Thor qua USB

1. Trên AYN Thor: **Settings → About → Tap "Build Number" 7 lần**
2. **Settings → Developer Options → Enable "USB Debugging"**
3. Cắm USB-C vào máy tính
4. Chấp nhận prompt "Allow USB Debugging" trên Thor

#### B. Kiểm tra kết nối

```powershell
# Trong PowerShell desktop
toolchain\platform-tools\adb.exe devices

# Expected output:
# List of devices attached
# ABC123456789    device
```

Nếu thấy "unauthorized" → Chấp nhận prompt trên Thor và chạy lại.

#### C. Cài APK

```powershell
# Install APK (-r = replace if exists)
toolchain\platform-tools\adb.exe install -r ThorNativeFG-DLSS-Quality.apk

# Expected output:
# Performing Streamed Install
# Success
```

#### D. Verify Installation

```powershell
# Check installed version
toolchain\platform-tools\adb.exe shell pm list packages | findstr thor

# Check logcat
toolchain\platform-tools\adb.exe logcat | findstr ThorNativeFG
```

---

## 🧪 Testing Sau Khi Cài

### Test 1: Visual Quality
1. Mở game trên Thor
2. Enable Frame Generation trong settings
3. Quan sát:
   - ✅ Edges smooth hơn (TAA)
   - ✅ Hình ảnh sharp sau upscale (CAS)
   - ✅ Text/HUD vẫn rõ
   - ✅ Không ghosting khi pan nhanh

### Test 2: Performance
```powershell
# Monitor frame times
adb logcat -s ThorNativeFG:V | findstr "ms\|fps"

# Expected: TAA ~0.35ms, CAS ~0.28ms, total <2ms
```

### Test 3: Adjustments (Nếu Cần)

Nếu gặp vấn đề:

| Issue | Parameter | Change |
|-------|-----------|--------|
| Ghosting (trailing) | taaBlendFactor | Tăng 0.05 → 0.10 |
| Flickering | taaBlendFactor | Giảm 0.15 → 0.08 |
| Quá sharp (halos) | casSharpness | Giảm 0.8 → 0.5 |
| Quá soft | casSharpness | Tăng 0.5 → 0.7 |
| Blurry HUD | casPreserveText | Ensure = 1 |

**Để điều chỉnh**: Sửa trong `native/engine.cpp`, rebuild APK, install lại.

---

## ⚡ Lộ Trình 3: Build Nhanh Chỉ Với Shader Improvements (NO C++ CODE)

Nếu **KHÔNG MUỐN** tích hợp TAA + CAS, chỉ dùng improvements hiện tại:

### Shaders Đã Cải Tiến (Từ Trước):
- ✅ `quality.comp` - Adaptive thresholds, pan detection
- ✅ `interpolate.comp` - Multi-stage blending
- ✅ `fxaa.comp` - Text detection
- ✅ `flow.comp` - Chroma-aware matching

### Build Chỉ Với Improvements Này:

```powershell
# Trong desktop PowerShell
cd C:\Users\dodan\thor-native-fg

# Build với config hiện tại (NO TAA/CAS)
python toolchain\build.py --sdk toolchain\sdk

# Install
toolchain\platform-tools\adb.exe install -r ThorNativeFG-improvements.apk
```

**Kết quả**: Frame generation stable hơn, FXAA tốt hơn, **NHƯNG CHƯA CÓ** TAA + CAS.

---

## 📊 So Sánh 3 Lộ Trình

| Lộ Trình | Quality | Performance | Effort | Time |
|----------|---------|-------------|--------|------|
| 1. APK cũ | 🟡 Good | ⚡ Fast | ⚡ Easy | 5 min |
| 2. DLSS-like (TAA+CAS) | ✅✅ Excellent | ⚡ Fast | 🔧 Moderate | 30 min |
| 3. Improvements only | ✅ Better | ⚡ Fast | 🔧 Moderate | 20 min |

---

## 🎯 Khuyến Nghị

### Nếu muốn DLSS-like quality TỐT NHẤT:
→ **Lộ Trình 2** (tích hợp TAA + CAS, build outside sandbox)

### Nếu muốn NHANH:
→ **Lộ Trình 1** hoặc **3** (dùng APK có sẵn hoặc build với improvements hiện tại)

---

## 🆘 Need Help?

Nếu gặp lỗi trong quá trình build/install:
1. Check build logs: Console output
2. Check device logs: `adb logcat | findstr ThorNativeFG`
3. Xem `DLSS-LIKE-UPGRADE-VI.md` section "Troubleshooting"
4. Backup APK cũ trước khi install mới

---

**🎮 Mục tiêu**: DLSS-like quality @ 1080p với răng cưa minimal, text sắc nét, 60fps stable!
