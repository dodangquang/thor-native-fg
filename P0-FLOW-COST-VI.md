# P0 — Dùng match-cost của flow cho nội suy khung hình

**Ngày:** 2026-10-26 · **Bản test:** 0.14-p0c (versionCode 17) · **Máy:** AYN Thor c2b670a0
(§1–§8 đo trên 0.14-p0a, §9 trên 0.14-p0b, §10 trên 0.14-p0c)

## 1. Vấn đề

`shaders/flow.comp` tính cost khớp ảnh cho cả hai chiều (forward/backward) để chọn
vector, rồi **vứt bỏ** sau khi chọn: `flowImage` (RGBA32F) đã dùng hết 4 kênh cho
hai vector. Hệ quả là `interpolate.comp` chỉ có thể suy độ tin cậy từ
`endpointConsistency = |forward + backward|`, và phép kiểm đó **chấp nhận một cặp
khớp sai nếu hai chiều cùng sai** (bề mặt lặp, vùng thiếu texture, khối bị che).

Ngoài ra `phase` đã có trong push constant nhưng bị hard-code `0.5f` ở
`engine.cpp` và không được đọc trong `interpolate.comp`, nên không thể mở đường 3x
(1/3, 2/3) sau này.

## 2. Thay đổi

| File | Nội dung |
|---|---|
| `shaders/flow.comp` | Thêm `binding=4` ảnh `rgba16f` `costImage`; ghi `vec4(costF, costB, 0, 0)` mỗi block. Thêm **early-out**: block có cost tìm kiếm mịn < 0,0025 ở cả hai chiều thì bỏ qua cửa sổ thô và bước tinh chỉnh (kết quả giữ nguyên bit, chỉ bỏ công vô ích). |
| `tests/reference/flow_serial.comp` | Cùng binding/định dạng để descriptor set layout còn tương thích với `flowReferencePipe`. |
| `shaders/interpolate.comp` | Thêm `binding=5` sampler cost; nhân thêm hệ số tin cậy `1 - smoothstep(0.035, 0.100, max(costF, costB))`; dùng `p.phase` thay vì hằng 0.5; hệ số bù sáng (fade) bị vô hiệu khi hai đầu warp khác cấu trúc (`mismatch > 0.02…0.15`). |
| `native/engine.hpp` | Thêm `Image flowCost`; `record(...)` nhận thêm `float phase = 0.5f`. |
| `native/engine.cpp` | Tạo `flowCost` (RGBA16F, kích thước grid) ở cả `init()` và nhánh `initSpatial()` có `motionTAA`; thêm binding vào `flowPipe`, `flowReferencePipe`, `interpolatePipe`; tăng pool descriptor (S 40→42, I 16→18); ghi descriptor; push `phase`; giải phóng `flowCost` trong `release()`. |
| `AndroidManifest.xml` | `versionCode 15`, `versionName 0.14-p0a` để phân biệt bản test trên máy. |
| `scripts/control.ps1` | Tạo `research/` trước khi ghi backup: lần chạy đầu trên cây chưa có thư mục này sẽ chết ở `Set-Content` (đã gặp thật trong lượt test). |
| `native/selftest.cpp` | Thêm hai dòng chẩn đoán: `SPRITE_REGIONS` (MAE theo lòng vật thể / dải biên / nền) và `SPRITE_BAD` (phân bố pixel sai > 16 theo khoảng cách tới biên), cùng check 3x (`TRIPLE`). Không đổi điều kiện PASS/FAIL cũ. |
| `native/presentation.hpp` | `SourceCadence::canTriple`, `TripleSchedule`, `planTriplePresentation`. |
| `native/layer.cpp` | Property `debug.thorfg.triple`; gọi `recordMulti` với pha 1/3 và 2/3; acquire 2 ảnh dự phòng; 3 present với 3 semaphore (sửa lỗi đọc ngoài mảng khi chỉ có 1 ảnh dự phòng). |
| `src/.../DemoActivity.java` | Xin chế độ hiển thị 120 Hz cùng độ phân giải để đo 3x. |

Chi phí bộ nhớ thêm: grid ≈ 240×135 @1080p × 8 B ≈ **260 KB**.

## 3. Bằng chứng trên Thor

### 3.1 Selftest GPU (`thor-selftest`, 256×144 → 512×288)

| Chỉ số | Trước (FE6B1D6A) | Sau (96BBD97D) |
|---|---|---|
| Tổng kiểm tra | 53, **1 FAIL** | 53, **1 FAIL** (y hệt) |
| `MIDPOINT mean_abs_error` | 0.0000 | 0.0000 |
| `textured_occluder max_rgb_mae` | 0.578 | 0.582 |
| `textured_occluder max_mean_luma_error` | 0.131 | 0.128 |
| `8x8_texture generated_mae` | 0.583 | 0.583 |

**Lưu ý quan trọng:** kiểm tra `textured object midpoint ... stable coverage and
luminance` **đã FAIL từ trước thay đổi này** (0.131 > ngưỡng 0.1). Bản 0.14-rc1
đang cài trên máy được build từ cây làm việc này, và cây làm việc đang có 21 file
sửa chưa commit. Đây là lỗi có sẵn, không do P0 gây ra; P0 làm nó giảm nhẹ
(0.131 → 0.128) nhưng chưa qua ngưỡng. Không nới ngưỡng test.

### 3.4 Sai số nằm ở đâu: đúng dải 8 px quanh silhouette

Thêm chẩn đoán chia sai số theo vùng so với hình chữ nhật kỳ vọng
(`SPRITE_REGIONS`, tổng trên 7 vị trí):

| Vùng | MAE | Số pixel |
|---|---|---|
| Lòng vật thể (cách biên ≥ 8 px) | **0.000** | 21.504 |
| Dải 8 px quanh biên | **4.314** | 32.256 |
| Nền | **0.000** | 204.288 |

Toàn bộ sai số nằm trong **một dải đúng 8 px** quanh biên vật thể — đúng một block
flow; lòng vật thể và nền warp chính xác tuyệt đối. Ba thí nghiệm xác nhận đây là
lỗi cấu trúc, không phải ngưỡng:

| Thí nghiệm | MAE | luma |
|---|---|---|
| Cổng tin cậy rộng 0.6–1.8 / 0.05–0.2 | 0.579 | 0.134 |
| Cổng hẹp 1.25–1.90 / 0.20–0.35 | 0.579 | 0.134 |
| Cổng gần như nhị phân 1.40–1.60 / 0.18–0.24 | 0.579 | 0.134 |
| Tinh chỉnh nghịch 4 bước thay vì 2 | 0.582 | 0.128 (dải 4.317) |

Cổng tin cậy gần như không can thiệp (ba mức cho kết quả trùng nhau), còn tăng số
bước tinh chỉnh làm dải **xấu hơn** vì vòng lặp hội tụ về vector sai của block.
Muốn qua ngưỡng phải có bước tinh chỉnh flow ở biên (mục 6.1), không phải chỉnh
hằng số.

Đo tiếp để biết dải rộng bao nhiêu thật (`SPRITE_BAD`, pixel có sai số > 16):

| Khoảng cách tới biên vật thể | 0 px | 1 px | 2 px | 3 px | ≥4 px |
|---|---|---|---|---|---|
| Số pixel sai (7 vị trí) | 326 | 870 | 585 | 52 | **0** |

Toàn bộ pixel sai nằm trong **1–2 px quanh biên** (1833 pixel / 7 vị trí ≈ 262 mỗi
vị trí, chu vi vật thể ≈ 288 px → đúng một vòng dày ~1 px). Đây không phải "dải
8 px" như tên gọi ban đầu: lỗi là **một đường 1–2 px** ở đúng chỗ khó xác định
pixel thuộc vật thể hay nền ở thời điểm giữa hai khung.

Hệ quả: để hết lỗi này cần biết **che khuất** (pixel đang bị che hay vừa lộ ra), tức
cần depth/motion-vector — không suy được từ hai ảnh màu. Đã thử và đều không đổi số:
nới/hẹp cổng tin cậy (3 mức), tăng vòng tinh chỉnh, chọn vector block hàng xóm theo
độ đồng thuận, và ép trộn ở chỗ bất đồng cực mạnh (làm case 8×8 tệ hơn: 0.583 →
0.833).

### 3.2 GPU stage benchmark (`thor-gpu-bench 1920 1080`, 3 lượt)

| Stage | Trước (ms) | Sau (ms) |
|---|---|---|
| flow | 0.994 / 0.996 / 0.996 | 0.995 / 0.999 / 0.996 |
| interpolate | 0.7208 / 0.7208 / 0.7209 | 0.7212 / 0.7212 / 0.7212 |
| **total** | 2.0211 / 2.0219 / 2.0238 | 2.0196 / 2.0239 / 2.0155 |

Chênh lệch nằm trong nhiễu đo (< 0.3 %). Ba lượt đọc cost thêm trong
`interpolate` không tạo chi phí thấy được vì block nhỏ (240×135) và đã bị che bởi
băng thông sẵn có. Đo lại sau khi máy chạy liên tục ~20 phút cho total
2.052–2.057 ms (flow ~1.00 ms): đây là trôi theo xung nhịp, đúng như cảnh báo sẵn
có trong repo rằng benchmark headless không khoá clock — nên chỉ so các lượt chạy
liền nhau.

### 3.5 Ổn định 45 giây trên đường layer (A/B cùng cảnh, cùng thứ tự)

Mỗi bản: 45 giây, `DemoActivity` mode 3 (đổi cảnh mỗi 5 giây), 44 cửa sổ telemetry:

| Chỉ số | 0.14-rc1 | 0.14-p0a |
|---|---|---|
| `source_fps` median | 29.99 | 29.99 |
| `display_fps` median | 59.93 | 59.93 |
| `gap_median_ms` | 16.831 | 16.829 |
| `gap_p95_ms` median | 25.249 | 25.248 |
| `gap_p95_ms` max | 42.081 | 50.493 |
| `cuts` / `holds` | 8 / 16 | 8 / 16 |
| `generated/sources` median | 0.954 | 0.955 |
| `fg_gpu_input_ms` median | 2.857 | 2.858 |

Hai bản trùng nhau trong sai số đo. Cần nói rõ: `gap_p95 ≈ 25 ms` ở đây **không
phải FG mất ổn định** — cảnh demo đổi scene mỗi 5 giây nên 8 cut/45 giây, và mỗi
cut kéo theo 2 frame holdoff; p95 cao là do các lần đổi cảnh đó. Muốn đo nhịp
thuần cần một cảnh chạy liên tục không cut.

### 3.6 Tối ưu 2x: bỏ việc vô ích ở block đã khớp hoàn hảo

Cửa sổ thô (±12) và bước tinh chỉnh (±1) chỉ có thể tìm **xa hơn**; khi bước mịn
đã cho cost < 0,0025 ở cả hai chiều (nền tĩnh, HUD, viền đen, vùng ít chi tiết) thì
hai bước sau chắc chắn không cải thiện. Bỏ chúng:

| Đo | Trước | Sau |
|---|---|---|
| `thor-gpu-bench` flow @1080p | 0.994–1.001 ms | **0.350–0.391 ms** |
| `thor-gpu-bench` total @1080p | 2.02–2.05 ms | **1.50–1.66 ms** (−25 %) |
| Layer thật `fg_gpu_input_ms` (1920×851) | 2.858 ms | **1.842 ms** (−36 %) |

Kết quả kiểm thử **không đổi một số nào** (flow translation 392/392, MIDPOINT
0.0000, band 4.314, mọi check khác như cũ): với block bị bỏ qua, vector giữ nguyên
bit; block có chuyển động thật vẫn chạy đủ ba bước vì cost cao. Nhịp trình bày cũng
không đổi (gap median 16.829 ms, p95 25.247 ms).

### 3.3 Đường layer thật (DemoActivity mode 3, swapchain 1920×851)

| Bản | `fg_gpu_input_ms` | `fg_gpu_output_ms` | generated | display_valid | gap median |
|---|---|---|---|---|---|
| 0.14-rc1 | 2.857 | 0.207 | 309 | 1 | 16.835 ms |
| 0.14-p0a | 2.858–2.865 | 0.208 | 278–491 | 1 | 16.834 ms |

Nguồn 30 FPS → hiển thị ~60 FPS, `flow=cooperative`, `presenter=async_queue`,
`sr=0` (tắt SR để cô lập FG). Ảnh chụp màn hình không thấy lỗi kết cấu.

## 4. Phạm vi: những gì **không** làm trong bản này

- **Occlusion endpoint selection** (chọn đầu nào khớp tốt hơn thay vì trộn) đã
  thử và **loại bỏ**: fixture `textured_occluder` cho MAE 0.578 → 0.601, tức
  không có lợi ích đo được. Muốn xử lý disocclusion đúng cần forward splatting +
  hole fill, không phải chọn một phía.
- **Linear-light + intermediate FP16**: hoãn có lý do. Toàn bộ fixture hiện tại
  sinh "chân lý" trong **không gian mã 8-bit** (`pattern()`), nên trộn trong
  linear light sẽ làm `MIDPOINT` và các case sprite **sai theo thiết kế fixture**
  dù đúng vật lý hơn. Cần dựng lại ground truth ở linear light trước.
- **3x**: chỉ mới có plumbing `phase` ở engine; layer vẫn phát một khung sinh ra
  cho mỗi khung thật (`planPairPresentation`). Bật 3x cần sửa scheduler + thêm
  một đích upscale thứ hai.
- **Gate theo tile, HDR/10-bit, proxy swapchain**: chưa đụng tới.

## 5. Hoàn tác

```powershell
# APK 0.14-rc1 đã cài trước khi test (versionCode 14)
adb install -r -d .p0-baseline\installed-base-0.14rc1.apk

# Nội dung 6 file trước/sau khi sửa
.p0-baseline\before   # trước P0
.p0-baseline\after    # sau P0
.p0-baseline\p0-review.diff
```

APK đã build trong lượt này:

- trước P0: `FE6B1D6AE38705AF3CEDE0C9C9CB0AC44273202CF2EF52397574B744ECCB889B`
- bản đang cài: **0.14-p0a** (versionCode 15), native
  `libVkLayer_ThorFG.so` SHA256 `BCB20873419216F0B3473954E42C97E0EEAD33843B982996FFE8930913014234`
  (bản trước khi tối ưu early-out là `9EAE5949…0825`; hash APK đổi mỗi lần build vì
  timestamp zip, nên đối chiếu bằng `.so`).

Trạng thái máy sau test: `debug.thorfg.enabled=0`, `target` rỗng, `sr=0`,
`gpu_debug_layers` đã xoá, tiến trình demo đã dừng. Bản 0.14-p0a **đang được cài**
để lượt sau test tiếp; runtime cũ vẫn giữ nguyên.

## 6. Bước tiếp theo (đề xuất)

1. **Che khuất ở biên**: đã xác định lỗi là đường 1–2 px và **không thể** giải bằng
   hai ảnh màu (mục 3.4 + mục 7). Hai lựa chọn thật: nối `TemporalEngine` với
   depth/velocity cho renderer có cung cấp (Eden/DroidDeck), hoặc chấp nhận ngưỡng
   hiện tại của fixture sprite là quá chặt so với phương pháp không có depth — nếu
   chọn cách sau thì phải sửa ngưỡng kèm đúng số đo này, không sửa âm thầm.
2. **Fixture linear-light**: sinh lại ảnh chuẩn bằng `srgb(lerp(linear(a),
   linear(b), t))`, rồi mới bật trộn tuyến tính + FP16 và đo lại.
3. **Per-tile gate**: dùng chính `costImage` để quyết định theo tile 16×16 thay vì
   một cờ `allow` toàn frame.
4. **3x**: đã chạy được (mục 8). Việc còn lại: nút bật/tắt trong UI + preset theo
   máy, và đo trên game thật (Eden/DroidDeck) chứ không chỉ cảnh demo.
5. **Đo nhịp trên cảnh không cut** để tách judder của FG khỏi judder do đổi cảnh
   (xem ghi chú ở 3.5).

## 7. 2x đã tối ưu chưa, và 3x cần gì

**Hiệu năng: đã.** Một khung sinh ở 1080p giờ tốn 1,50–1,84 ms tuỳ nội dung (trước
là ~2,0–2,9 ms). Phần còn lại là `interpolate` ~0,72 ms và các bản copy ~0,17 ms;
flow đã xuống 0,35–0,39 ms. Muốn nhanh hơn nữa phải giảm số mẫu trong `cost()`
(16 → 9), tức đánh đổi chất lượng khớp — chưa nên làm khi chưa có số đo trên nội
dung game thật.

**Chất lượng: chưa, và đã xác định được giới hạn thật.** Sai số còn lại là một
đường **1–2 px** đúng trên biên vật thể (1833 pixel có sai số > 16 trên 7 vị trí;
không có pixel sai nào cách biên ≥ 4 px — xem 3.4). Đây là chỗ một pixel hoặc thuộc
vật thể hoặc thuộc nền ở thời điểm giữa hai khung, và hai ảnh màu không đủ để quyết
định. Năm hướng rẻ đã thử đều không cải thiện (nới/hẹp cổng, tăng tinh chỉnh, chọn
vector hàng xóm, ép trộn khi bất đồng mạnh). Muốn hết cần **depth/motion-vector** —
đúng contract đã có sẵn trong `native/temporal.hpp` — hoặc một mô hình học máy; cả
hai đều là việc lớn hơn một buổi. Vì vậy 2x nên được coi là **ổn định và đã tối ưu
hiệu năng**, còn "sạch tuyệt đối ở biên" là hạng mục cần dữ liệu depth/MV.

**3x (2 khung sinh cho mỗi khung thật).** Chỉ có lợi khi nguồn dưới ~40 FPS và màn
hình 120 Hz (30 → 90, 40 → 120). Panel Thor có cả 60 và 120 Hz, nhưng app demo đang
chọn 60 Hz nên muốn đo 3x phải có surface 120 Hz. Ba việc:
1. Engine: sinh hai ảnh đệm ở pha 1/3 và 2/3 (`phase` đã có sẵn), thêm cặp
   `mid2/upMid2` + descriptor set; chi phí thêm ~0,7 ms cho mỗi khung thật.
2. Layer: `planPairPresentation` hiện lên lịch 1 khung sinh/1 khung thật; cần lịch
   3 khe (0, 1/3, 2/3), hai ảnh swapchain dự phòng và hai mốc present.
3. Chất lượng: khe 1/3 và 2/3 rộng hơn nên lỗi biên/disocclusion nặng hơn 2x — nên
   làm occlusion trước rồi mới bật 3x, hoặc chỉ bật 3x cho nội dung ít chuyển động.

## 8. 3x — đã chạy thật trên Thor

**Bật:** `adb shell setprop debug.thorfg.triple 1` (tắt: `... 0`). Chỉ có hiệu lực
khi surface 120 Hz và nguồn đủ chậm (`interval ≥ 2.9 × refresh`, tức dưới ~40 FPS ở
120 Hz). App demo giờ tự xin chế độ 120 Hz khi mở.

**Ba thay đổi chính:**

1. `Engine::recordMulti(..., phases[], count)` — một lượt analyze/flow/quality, rồi
   nội suy nhiều pha vào `mid`/`mid2` và upscale ra `upMid`/`upMid2`. Cần thêm 2 ảnh,
   2 descriptor set và nới pool (S 42→49, I 18→21, maxSets 20→23).
2. **Lỗi gốc phải sửa trong shader:** warp luôn dùng hằng `0.5` (`uv-0.5*flow`), nên
   mọi pha cho ra *cùng một* ảnh giữa. Giờ warp theo `t`: `aUV=uv-t*flow.xy`,
   `bUV=uv-(1-t)*flow.zw`. Với `t=0.5` kết quả 2x không đổi một bit.
3. Layer: `planTriplePresentation` (3 mốc 1/3, 2/3, 1), acquire **hai** ảnh dự phòng,
   3 present. Nếu ảnh dự phòng thứ hai bận, frame đó tự lùi về 2x.

**Số đo (nguồn 30 FPS, surface 1920×851 @120 Hz, 25 giây/lượt):**

| | 2x | 3x |
|---|---|---|
| `display_fps` median | 59.93 | **89.67** |
| `gap_median_ms` | 16.829 | **8.412** |
| `generated/sources` median | 0.934 | **1.874** |
| `fg_gpu_input_ms` median | 1.843 | 2.922 |
| `cuts` / `holds` | 4 / 8 | 4 / 8 |

Chi phí thêm cho khung sinh thứ hai: **+1,08 ms** (một dispatch interpolate + một
upscale + một blit ở độ phân giải hiển thị).

**Kiểm thử:** thêm check `3x produces distinct 1/3 and 2/3 midpoints from one flow
pass` — ảnh 1/3 và 2/3 khớp đúng vị trí thời gian của mình (`own=0.0001` so với
`other=5.9251`). Tổng 54 check, 1 FAIL quen thuộc ở biên vật thể.

**Đã sửa một lỗi do chính thay đổi này gây ra:** mảng semaphore luôn đọc
`s.ready[extra2]` kể cả khi không có ảnh dự phòng thứ hai → SIGSEGV ở đường 2x.
Phát hiện nhờ chạy lại 2x sau khi làm 3x; giờ chỉ đụng `extra2` khi thực sự acquire
được.

**Giới hạn:** cần 120 Hz; chỉ đáng khi nguồn dưới ~40 FPS; lỗi biên 1–2 px sẽ lộ rõ
hơn ở 3x vì khe thời gian rộng hơn.

## 9. 3x trong UI và đo trên game thật

**Nút bật/tắt:** checkbox *"3× bù khung hình · thêm 1 khung sinh cho mỗi khung gốc"*
ở màn hình chính, ngay dưới nhóm khử răng cưa (kèm một dòng giải thích điều kiện).
Bật/tắt được lưu vào `SharedPreferences` (`triple`) **và** ghi `debug.thorfg.triple`.
Layer đọc property này ở **mỗi khung hình** nên đổi được khi game đang chạy, không cần
mở lại. `AutoMode.configure()` ghi lại property mỗi lần bật 1-chạm, nên lựa chọn giữ
qua reboot. HUD đọc thêm trường `triple` trong `THOR_METRICS` và hiện `· 3×`, ví dụ
`Gốc 30 | FG 90 · 3× | SR TẮT`.

**Sống qua reboot:** `debug.*` không bền, nên app lưu thêm `persist.thorfg.triple`
(persist thì sống qua reboot) và module `service.sh` chép nó vào `debug.thorfg.triple`
lúc khởi động. Bản module đã cài trên máy cần **flash lại zip** để có dòng đó; trước khi
flash, app vẫn tự đồng bộ: mỗi lần mở app, `onResume` so sánh checkbox với property và
ghi lại nếu lệch (im lặng, không đổi dòng trạng thái).

**Đo trên game thật — Eden Nightly + Dark Souls Remastered (cap 30 FPS), 1920×1080,
surface 120 Hz, bật/tắt nóng giữa hai lượt 20 giây, cùng cảnh:**

| | 2x | 3x |
|---|---|---|
| `source_fps` | 30.00 | 30.00 |
| `display_fps` | 58,9–59,9 | **89,6** |
| `gap_median_ms` | 16,829 | **8,415** |
| `gap_p95_ms` | 16,835 | 16,830 |
| `generated/sources` | 1,00 | **2,00** |
| `fg_gpu_input_ms` | 1,415 | 2,280 |
| thời gian trong `vkQueuePresentKHR` | ~16,1 ms | ~18,3 ms |

Ảnh title screen Dark Souls hiển thị nguyên vẹn (không rách, chữ không nhoè) — cùng
kết quả với cảnh demo.

**Cổng an toàn chạy đúng trên game nhanh:** AetherSX2 + Shadow of the Colossus (1080p)
dao động 45–50 FPS → `triple=0` (interval 19–23 ms < 24,2 ms), layer tự giữ 2x, không
khung nào bị chia quá 1/3 nhịp. Đúng thiết kế: 3x chỉ dành cho nguồn ≤ ~40 FPS.

**Giới hạn mới đo được:** Eden chạy presenter `fence` (đồng bộ ngay trong
`vkQueuePresentKHR`), nên mỗi khung thật bị giữ ~18,3 ms ở 3x (~16,1 ms ở 2x) trong
ngân sách 33 ms của game 30 FPS. Vẫn đủ, nhưng game nào sát trần 33 ms thì nên dùng 2x
hoặc chuyển presenter async — ghi nhận, chưa sửa trong bản này.

**Kiểm thử tự động:** `thor-presentation-test` 42 check (thêm 9 check 3x: ngưỡng
`canTriple` cho 30/60/40/25 FPS, lịch 1/3–2/3, từ chối khi hàng đợi đã trễ, không trôi
pha, không tích luỹ deadline) và `RuntimeMetricsTest` 38 check (thêm 6 check: HUD hiện
`· 3×`, protocol v1 không được claim 3x, giá trị lạ bị loại, swapchain không hợp lệ
không quảng cáo 3x).

**Phiên bản:** 0.14-p0b (versionCode 16), APK `ThorNativeFG-debug.apk`
`D1333FFD…0926` (bản 0.14-p0a trước khi thêm UI: `F439026C…7B`). Native
`libVkLayer_ThorFG.so` `E808C54F…1D2`: so với bản đo ở §8 chỉ thêm trường telemetry
`triple=` vào `THOR_METRICS` (đường present không đổi), nên các số ở §8 vẫn dùng được.

## 10. Tối ưu 3x: gộp hai pha nội suy vào một dispatch

**Đo trước khi sửa để biết tiền đi đâu.** `thor-gpu-bench 1920 1080` (thêm tham số
`triple` để chạy 2 pha): một pha nội suy 0,891 ms, hai pha gọi riêng 1,596 ms → pha thứ
hai tốn **0,705 ms**, cộng 0,085 ms blit. Tổng 3x 2,390 ms so với 2x 1,651 ms =
**1,45×**.

**Vì sao gộp được.** Hai pha chỉ khác `tWarp`; phần còn lại (flow tại pixel, match-cost,
`current`, kiểm tra mảng tĩnh) giống hệt và đang bị đọc hai lần. `interpolate2.comp` giữ
nguyên công thức của `interpolate.comp` nhưng nhận `phase0`/`phase1` và ghi hai ảnh
(`binding 3` và `binding 6`) trong **một** dispatch.

**Thay đổi:** shader mới `shaders/interpolate2.comp`; `Engine::interpolate2Pipe` +
`interpolate2Sets[2]` (layout `{S,S,S,I,B,S,I}`, push constant 28 byte); `recordMulti`
rẽ nhánh — `count>1` dùng dispatch gộp, `count==1` vẫn dùng `interpolate.comp` nên đường
2x không đổi; pool storage-image 21→23; bỏ `interpolateSets2`.

**Bằng chứng tương đương:** selftest thêm check *"fused3x dispatch matches the
single-phase shader bit for bit"* — chạy 2 pha gộp, rồi chạy lại từng pha bằng pipeline
một pha, so từng byte: `fused_vs_single_diff=0,0` (bit-identical). Check cũ giữ nguyên:
`TRIPLE own_1_3=0.0001 other_1_3=5.9251`, `pixel60_60 mid=82 mid2=94` đúng giá trị mong
đợi.

**Benchmark (tỉ lệ 3x/2x để loại ảnh hưởng xung nhịp):**

| | trước | sau |
|---|---|---|
| `interpolate` 1 pha | 0,891 | 1,032 (cùng code) |
| `interpolate` 2 pha | 1,596 | **1,163** |
| `finish_copies` 2 pha | 0,289 | 0,304 |
| tổng 2 pha | 2,390 | **2,016** |
| **tỉ lệ 3x / 2x** | **1,45×** | **1,07×** |

**Trên máy thật** (mỗi lượt lấy median của 22 dòng `THOR_METRICS` liên tiếp, cùng cảnh,
bật/tắt nóng bằng `setprop`):

| | 2x | 3x trước | 3x sau |
|---|---|---|---|
| Demo 1920×851 · `fg_gpu_input_ms` | 1,841 | 2,922 | **2,176** |
| Demo · `display_fps` | 59,95 | 89,67 | 89,66 |
| Eden + Dark Souls 1080p · `fg_gpu_input_ms` | 1,240 | 2,280 | **1,810** |

Chi phí thêm của 3x giảm từ **+1,08 ms → +0,34 ms** (demo, presenter async) và
**+0,86 ms → +0,57 ms** (Eden, presenter `fence` nên nhiễu hơn). Nhịp hiển thị không đổi
vì đã sát trần 90 khung/giây; phần thu được là GPU rảnh hơn, ít nhiệt hơn và ít trễ hơn.

**Còn tối ưu được nữa không — đã đo, biên còn lại nhỏ:**
- *Blit trùng:* khung sinh bị blit `mid → upMid` rồi layer blit `upMid → swapchain`; khi
  `input == output` bước đầu là copy thừa ~0,085 ms/khung ×2. Muốn bỏ phải đổi API
  engine↔layer (layer đọc thẳng `mid`/`mid2`) — chưa làm.
- *`interpolate` giờ là chi phí lớn nhất* (~1,16 ms cho 2 pha, 11 lần lấy mẫu/pixel/pha).
  Hai hướng còn lại: bỏ 1 vòng tinh chỉnh (đổi chất lượng — phải đo lại MAE) hoặc lưu
  `flow` bằng RGBA16F thay RGBA32F (giảm nửa băng thông đọc flow, đổi độ chính xác
  ~0,01 px).
- *Nhịp hiển thị đã sát trần:* 89,66 khung/giây với nguồn 30 FPS (trần 90).

**Kiểm thử:** selftest 55 check (54 cũ + check gộp), 1 FAIL quen thuộc ở biên vật thể;
`thor-presentation-test` 42/42; `RuntimeMetricsTest` 38/38.

**Bản:** 0.14-p0c (versionCode 17), APK `ThorNativeFG-debug.apk` `3542B4F3…1F6B`, native
`libVkLayer_ThorFG.so` `5D435592…6B9C`.
