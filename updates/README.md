# Cập nhật qua git (không dùng Netlify)

Ứng dụng **Thor Native FG** tự kiểm tra bản mới bằng cách đọc feed JSON trong
repository này:

```
https://raw.githubusercontent.com/dodangquang/thor-native-fg/master/updates/v1/com.thor.nativefg.json
```

APK phát hành là **GitHub Release asset** trong cùng repo:
`releases/download/thorfg-v<version>/ThorNativeFG-<version>.apk`.

## Vì sao git thay vì Netlify

- Mỗi lần phát hành chỉ là 1 commit + 1 tag + 1 release ⇒ không tốn build minute,
  không phụ thuộc dịch vụ ngoài.
- Raw GitHub được cache CDN nên vẫn nhanh cho người dùng.
- Feed **không phải nguồn tin cậy**: app luôn kiểm tra lại dung lượng, SHA-256,
  package, versionCode và **chữ ký APK** trùng với bản đang cài trước khi mở
  trình cài đặt. Feed bị sửa cũng không thể cài APK lạ.

## Quy trình phát hành

```powershell
# 1. Tăng versionCode/versionName trong AndroidManifest.xml (script sẽ nhắc)
# 2. Build + kiểm tra + cập nhật feed (không upload):
.\tools\publish-release.ps1 -VersionCode 21 -VersionName 0.16.0

# 3. Commit feed, tạo tag và release (cần đăng nhập git + gh):
git add AndroidManifest.xml updates/v1/com.thor.nativefg.json
git commit -m "release: 0.16.0"
git push origin master
git tag thorfg-v0.16.0 && git push origin thorfg-v0.16.0
gh release create thorfg-v0.16.0 ThorNativeFG-0.16.0.apk --title "Thor Native FG 0.16.0"
```

`publish-release.ps1 -Publish` sẽ tự chạy các lệnh git/gh trên nếu môi trường đã
đăng nhập sẵn.

## Định dạng feed

```json
{
  "schemaVersion": 1,
  "packageName": "com.thor.nativefg",
  "channel": "full",
  "versionCode": 21,
  "versionName": "0.16.0",
  "minSdk": 29,
  "bytes": 1050000,
  "sha256": "<64 hex>",
  "apkUrl": "https://github.com/dodangquang/thor-native-fg/releases/download/thorfg-v0.16.0/ThorNativeFG-0.16.0.apk",
  "notesVi": "…",
  "notesEn": "…"
}
```

Giới hạn trong code: URL APK chỉ chấp nhận đúng host `github.com` và đúng
đường dẫn `/<owner>/<repo>/releases/download/thorfg-v<version>/ThorNativeFG-<version>.apk`;
URL feed chỉ chấp nhận đúng một chuỗi ở `UpdateSpec.manifest()`.
