# Thor Native FG

Vulkan layer bù khung hình (frame generation) + khử răng cưa cho **AYN Thor**
(Snapdragon 8 Gen 2, Adreno 740, Android 13). Layer được nạp bằng Zygisk nên
không cần chỉnh sửa app giả lập.

## Thành phần

| Đường dẫn | Vai trò |
|---|---|
| `native/layer.cpp` | hook `vkQueuePresentKHR`/`vkAcquireNextImageKHR`, chính sách 2×/3×, metrics |
| `native/engine.cpp` | flow quang học 2 tầng (coarse 32×18 + fine 64×36), nội suy, AA |
| `shaders/` | flow, interpolate, FXAA/SMAA, quality gate |
| `src/com/thor/nativefg/` | app điều khiển: 1-chạm, HUD, preset giả lập, cập nhật, bản quyền |
| `updates/v1/` | feed cập nhật qua git ([updates/README.md](updates/README.md)) |
| `tools/` | phát hành bản mới + cấp key bản quyền |
| `scripts/` | tiện ích đo nhiệt/nhịp, preset Eden, DroidDeck |

## Build

```powershell
.\build.ps1 -Sdk "toolchain\sdk"          # ra ThorNativeFG-debug.apk
adb install -r ThorNativeFG-debug.apk
```

Bản phát hành (tăng version + ghi feed):

```powershell
.\tools\publish-release.ps1 -VersionCode 21 -VersionName 0.16.0
```

## Cập nhật qua git (không Netlify)

App đọc `updates/v1/com.thor.nativefg.json` từ `raw.githubusercontent.com` của
repo này và tải APK từ GitHub Release. Trước khi mở trình cài đặt, app kiểm tra
lại **package, versionCode, versionName, minSdk, dung lượng, SHA-256 và chữ ký
APK** khớp với bản đang cài. Chi tiết: [updates/README.md](updates/README.md).

## Bản quyền

- App chỉ chứa **public key** (`assets/thorfg-license.json`).
- Key phát hành offline, dạng `THORFG1.<payload>.<signature>` (RSA-2048,
  SHA-256), có thể gắn thiết bị và/hoặc ngày hết hạn.
- Cấp key: `.\tools\license-issue.ps1 -Device <16 hex> -Days 365 -Note "..."`
  (file key ghi vào `keys/`, đã git-ignore).
- Khoá riêng tư nằm ở `private/thorfg-license-private.xml` — **không commit**.
  Mất khoá này thì mọi key đã phát hành không xác minh được nữa.
- `enforce` trong asset đang là `false`: app chạy bình thường, mục bản quyền chỉ
  hiển thị trạng thái. Đổi thành `true` để chặn nút bật khi chưa có key/dùng thử.

## Kiểm thử

```powershell
# GPU selftest trên máy (flow, nội suy, fused 3x, AA)
adb push build\thor-selftest /data/local/tmp/ ; adb shell "chmod 755 /data/local/tmp/thor-selftest; /data/local/tmp/thor-selftest"

# JVM test cho metrics HUD và preset DroidDeck
```

## Ghi chú thiết bị

- Panel 1080×1920 có 2 mode: `id=1` 60Hz, `id=2` 120Hz. Layer chỉ nhân đôi khi
  nguồn ≤ ~½ nhịp màn hình; 30 FPS → 60 (2×) hoặc 90 (3× trên 120Hz).
- AA chỉ áp cho khung 2×; 3× giữ khung thô để đủ ngân sách.
- `debug.thorfg.*` dùng cho chế độ thủ công; chế độ tự động đọc
  `/data/adb/thor-native-fg/auto.conf` do app ghi.
