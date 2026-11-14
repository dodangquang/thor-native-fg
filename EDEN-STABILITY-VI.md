# Eden ổn định hơn — cấu hình đã áp dụng (26/10/2026)

Mục tiêu: hạn chế crash của Eden khi chơi game Switch, **giữ nguyên frame gen (FG) của Thor**.
Áp dụng cho cả hai app Eden đang cài: `dev.eden.eden_emulator.nightly` (Eden Thor dùng chung
file qua bind mount) và `dev.igawa6.edenduo`.

## Vì sao lại sửa mấy mục này

Crash 16:38 hôm nay (STORY OF SEASONS: Grand Bazaar) kết thúc bằng hai dòng trong log Eden:

```
video_core/engines/puller.cpp: assert Unimplemented engine 0000
video_core/renderer_vulkan/maxwell_to_vk.cpp:242 SurfaceFormat: unreachable unknown format 255
```

Eden có PR đúng lỗi này: [#4529 "handles unreachable unknown format 255 error"](https://git.eden-emu.dev/eden-ci/pr/releases/tag/4529)
(14/10/2026). Nội dung PR: lỗi xảy ra **khi bật Async GPU** (họ gặp ở Ender Magnolia) và bản sửa
trả về `VK_FORMAT_UNDEFINED` thay vì `abort()`. Bản sửa đó **chưa vào nightly**: nightly mới nhất
là 18/10 (`de7e6e01d4`) — đúng bằng bản đang cài. Vì vậy cách xử lý hiện tại là tắt Async GPU.

## Đã đổi

| Khóa trong `config.ini` | Trước | Sau | Lý do |
|---|---|---|---|
| `use_asynchronous_gpu_emulation` | `true` | **`false`** | nguyên nhân đã biết của crash `unknown format 255` |
| `gpu_log_vulkan_calls` | `true` | `false` | ghi log **mọi** lệnh Vulkan → rất chậm, không cần khi chơi |
| `gpu_log_memory_tracking` | `true` | `false` | chỉ để debug cấp phát |
| `gpu_log_driver_debug` | `true` | `false` | chỉ để debug driver |

Đường dẫn:

- `/sdcard/Android/data/dev.eden.eden_emulator.nightly/files/config/config.ini`
  (Eden Thor `relWithDebInfo` dùng chung file này nhờ bind mount của module)
- `/sdcard/Android/data/dev.igawa6.edenduo/files/config/config.ini`

Sao lưu bản gốc: `research/eden-setup/config.ini.bak`, `research/eden-setup/edenduo-config.ini`,
và trên máy `/sdcard/Download/config.ini.bak`.

## Bằng chứng đã có hiệu lực

Log Eden ghi lại đúng giá trị mới:

```
Config <Info> common/settings.cpp:137:LogSettings: MC Renderer.use_asynchronous_gpu_emulation: false
```

## Đã kiểm chứng

- Mở lại **STORY OF SEASONS: Grand Bazaar** bằng FG tự động (2×, 1920×1080, nguồn ~30 FPS):
  chạy **hơn 9 phút / 16.325 khung nguồn**, process sống, `generated_fps` = `source_fps` = 30,
  **0 dòng `<Critical>`** trong log Eden, **không sinh tombstone mới** (32 → 32).
- Trước đó cùng ngày: bật/tắt 2×↔3× liên tiếp 12 lần giữa chừng, không crash.

## Nếu vẫn có game crash

1. Xem `.../files/log/eden_log.txt`, tìm dòng `<Critical>` cuối cùng — nó chỉ ra chính xác chỗ Eden vấp.
2. Nếu là lỗi dịch lệnh GPU khác: bật độ chính xác cao hơn **cho riêng game đó**
   (Settings → System → GPU Accuracy = High). Chậm hơn nhưng dịch lệnh kỹ hơn.
3. Nếu crash cả khi **đã tắt FG**: đó là lỗi của emulator, không phải Thor FG —
   thử cập nhật Eden hoặc đổi GPU driver trong Eden.
4. Muốn loại một game khỏi FG: app Thor → **Tùy chọn khác → Danh sách game tự động** → bỏ chọn game đó.

## Cách hoàn tác

```powershell
adb push research/eden-setup/config.ini.bak /sdcard/Download/config.ini.bak
adb shell "su -c 'cat /sdcard/Download/config.ini.bak > /sdcard/Android/data/dev.eden.eden_emulator.nightly/files/config/config.ini'"
```

(hoặc đổi 4 khóa ở bảng trên từ `false` về `true`). Tương tự với `edenduo-config.ini`.

## Ghi chú

- Async GPU tắt: bớt được kiểu crash này, đổi lại kém mượt hơn một chút ở game nặng.
  Khi Eden merge PR #4529 vào nightly, cập nhật nightly rồi có thể bật lại Async GPU.
- FG của Thor không nằm trong đường crash: tombstone không có frame nào của `libVkLayer_ThorFG.so`,
  và layer không hook `vkCreateImage`/`vkCreateBuffer` nên không thể sinh ra format 255.
