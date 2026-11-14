# DroidDeck + Minecraft Dungeons II — chỉnh cho ổn định (26/10/2026)

## Hiện trạng đã kiểm tra

| Thành phần | Giá trị trên máy |
|---|---|
| DroidDeck | **0.6.0** (versionCode 15), cài 23/10 |
| Driver đang chọn | `Mesa_Turnip_v26.3.0-20261013-r2` (Vulkan 1.4.363, KGSL, có A7xx) |
| Driver khác đã cài | `DD-Turnip_v0.2.0`, `Mesa_Turnip_v26.3.0-20261013` (+ bản `-Linux` tương ứng) |
| Upscaler của DroidDeck | `upscaler=8` (GSR quality), `upscaleSharpness=50` |
| Frame gen của DroidDeck | `frame_gen.xml`: `engine=off`, `multiplier=2`, `target=0` |
| Game | `/sdcard/Games/Pc Games/Minecraft Dungeons II/Dungeons.exe`, 10 GB, launch args `-d3d11` |
| Game engine | UE (thư mục `Dungeons2/Saved/Config/Windows`), render bằng **DX12/vkd3d** |

## Vì sao hay khựng rồi crash

Crash report của chính game (`Saved/Crashes/UECC-Windows-*/CrashContext.runtime-xml`):

```
CrashType      = Assert
ErrorMessage   = Abort signal received
PCallStack     = ucrtbase → d3d12core → Dungeons-Win64-Shipping
Misc.PrimaryGPUBrand = Turnip Adreno (TM) 740
SecondsSinceStart = 0
```

⇒ Game chạy **D3D12 qua vkd3d-proton** (`d3d12core.dll`) và abort ngay trong vkd3d. Đây là đường
nặng và rủi ro nhất trên Turnip/Adreno: vkd3d dịch DX12 → Vulkan, shader được dịch dần trong lúc
chơi ⇒ **khựng mỗi khi gặp shader mới**, và gặp lỗi là abort cả tiến trình.

Tham số `-d3d11` trong DroidDeck **không có tác dụng** với bản game này (stack vẫn là d3d12core),
nên không thể hạ xuống D3D11/DXVK bằng launch option.

## Đã thay đổi

**1. Game settings** — `.../compatdata/2921179835/pfx/drive_c/users/steamuser/AppData/Local/Dungeons2/Saved/Config/Windows/GameUserSettings.ini`

| Khóa | Trước | Sau | Lý do |
|---|---|---|---|
| `ResolutionSizeX/Y` | 1280×720 | **960×540** | đúng **2×** để GSR upscale lên 1920×1080, GPU nhẹ hơn nhiều |
| `FrameRateLimit` | 60 | **40** | ổn định khung, và 40 FPS là mốc 3× của Thor FG trên 120 Hz |
| `sg.AntiAliasingQuality` | 1 | **0** | tắt TAA của game (đỡ nhoè + đỡ GPU); khử răng cưa để lớp compositor lo |
| các `sg.*Quality` khác | 0–1 | **0** | giảm tải GPU/VRAM để không khựng |
| `bUseVSync` | False | giữ False | VSync trong game + compositor gây khựng |

Bản gốc: `research/droiddeck/GameUserSettings.ini.bak`.

**2. App Thor Native FG (0.14-p0d):** preset DroidDeck trước đây chặn cứng phiên bản 0.5.0 (14),
nên trên DroidDeck 0.6.0 bấm nút là báo lỗi. Nay chấp nhận **0.6.0 (15)** — 3 khóa preset quản lý
(`upscaler`, `screenEffects`, `frame_gen.engine`) vẫn cùng kiểu dữ liệu. Thêm test cho đúng dữ liệu
0.6.0 thật (21 check).

## Nên chọn gì trong DroidDeck

- **Upscale:** giữ upscaler của DroidDeck = **GSR quality** (`upscaler=8`), sharpness **35–50**.
  Với nguồn 960×540 → 1920×1080 (2×) đây là cấu hình cho ảnh nét nhất mà GPU không phải làm thêm.
- **Frame gen:** DroidDeck `engine=off` (đang đúng) để **Thor FG** làm; đừng bật cả hai.
- **Khử răng cưa:** nếu thấy răng cưa ở 960×540, bật FXAA trong `screenEffects`
  (`"fxaa":true`) — chạy trong compositor, rẻ; ngược lại để `false` cho ảnh nét hơn.
- **Preset "Thor SR"** trong app (`Tùy chọn khác → DroidDeck · dùng SR của Thor`) đặt upscaler về
  `linear` + hiệu ứng trung hòa — chỉ dùng khi muốn **lớp Thor** tự upscale. Với game PC chạy qua
  gamescope, upscale của DroidDeck thường là đường thực tế hơn; preset có nút hoàn tác.

## Phát hiện khi chơi trực tiếp: FG đang chạy nhưng khung sinh bị bỏ

Đo trên máy lúc Minecraft Dungeons II đang chạy (DroidDeck, 20:20):

```
source_fps=58.13  generated_fps=51.23   (lớp Thor trình bày ~109 khung/giây)
display_fps=59.00 display_generated_fps=15.00   (màn hình chỉ quét 60 khung/giây)
fg_cpu_processing_ms=10.724  fg_gpu_input_ms=2.381  presenter=async_shared
gap_max_ms=33.3 … 366.7
```

Nghĩa là: surface của DroidDeck chỉ hiển thị **60 Hz**, nhưng lớp Thor tưởng là 120 Hz
(vì `vkGetRefreshCycleDurationGOOGLE` trả ~16,7 ms rồi bị chia đôi trong code) nên vẫn sinh
thêm khung. Kết quả: **~109 khung trình bày/giây nhưng chỉ 60 khung lên màn hình** — phần còn
lại bị bỏ, đổi lấy **~10,7 ms CPU mỗi lần present** và 2,4 ms GPU mỗi khung nguồn. Đây là một
nguyên nhân trực tiếp của cảm giác khựng.

**Đã sửa trong layer (0.14-p0d, APK `C8836949…0C47`):** mỗi cửa sổ đo 1 giây, nếu
`(khung nguồn + khung sinh) > 1,2 × tốc độ màn hình thực đo` hai lần liên tiếp thì **tự dừng sinh
khung** cho swapchain đó và ghi `THOR_GENERATION_PAUSED`. Đã kiểm chứng trên máy:

```
THOR_GENERATION_PAUSED presented=118.87/s displayed=59.98/s refresh_ns=8333333
… source_fps=59.96 generated_fps=0.00 display_fps=59.97 fg_cpu_processing_ms=0.000 generation_blocked=1
```

tức là sau ~2 giây, chi phí present về 0 và màn hình vẫn 60 FPS đều.

**Lưu ý:** game đang chạy giữ layer cũ trong bộ nhớ; cần **thoát DroidDeck và mở lại** để nhận
bản mới. Trong lúc đó `debug.thorfg.auto=0` đã được đặt để tắt FG ngay cho phiên đang chơi.

## Nếu vẫn khựng/crash — thử theo thứ tự

1. **Đổi driver Turnip**: DroidDeck → Settings → GPU driver → `DD-Turnip v0.2.0` (bản DroidDeck tự
   đóng gói), rồi chạy lại game. Đây là thay đổi 1 chạm, hoàn tác dễ.
2. Hạ tiếp độ phân giải game xuống **854×480** trong `GameUserSettings.ini` nếu 960×540 vẫn khựng.
3. Thử **tắt Thor FG** cho DroidDeck (bỏ `com.droiddeck.launcher` khỏi danh sách tự động trong app)
   để loại trừ hoàn toàn lớp FG.
4. Nếu crash vẫn nằm trong `d3d12core` → đó là giới hạn của vkd3d trên Turnip với tựa game này;
   cách duy nhất còn lại là chờ vkd3d/Turnip mới hoặc tìm bản game có DX11 thật.

## Hoàn tác

```powershell
# game settings
adb push research/droiddeck/GameUserSettings.ini.bak /sdcard/Download/g.ini
adb shell "su -c 'cat /sdcard/Download/g.ini > /data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/steamapps/compatdata/2921179835/pfx/drive_c/users/steamuser/AppData/Local/Dungeons2/Saved/Config/Windows/GameUserSettings.ini'"
```

Preset DroidDeck: bấm lại nút trong app để hoàn tác (app tự lưu bản gốc).
