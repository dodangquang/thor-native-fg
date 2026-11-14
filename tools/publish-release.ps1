param(
    [Parameter(Mandatory=$true)][int]$VersionCode,
    [Parameter(Mandatory=$true)][string]$VersionName,
    [string]$NotesVi = "Bản cập nhật mới.",
    [string]$NotesEn = "New update.",
    [string]$Sdk = "toolchain\sdk",
    [string]$PrivateKey = "private\thorfg-license-private.xml",
    [switch]$Publish
)
# Builds the signed APK, then writes updates/v1/com.thor.nativefg.json from the
# real file (size + SHA-256). The feed is served from this git repository, so
# publishing is a commit + tag + release, never a Netlify deploy.
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$manifest=Join-Path $root 'AndroidManifest.xml'
$feed=Join-Path $root 'updates\v1\com.thor.nativefg.json'
$apkName="ThorNativeFG-$VersionName.apk"
$releaseApk=Join-Path $root $apkName

$text=[IO.File]::ReadAllText($manifest)
$text=[regex]::Replace($text,'android:versionCode="\d+"',"android:versionCode=`"$VersionCode`"")
$text=[regex]::Replace($text,'android:versionName="[^"]*"',"android:versionName=`"$VersionName`"")
[IO.File]::WriteAllText($manifest,$text,[Text.UTF8Encoding]::new($false))
Write-Host "Manifest set to $VersionCode / $VersionName"

& (Join-Path $root 'build.ps1') -Sdk (Join-Path $root $Sdk)
$built=Join-Path $root 'ThorNativeFG-debug.apk'
if(-not (Test-Path $built)){throw "build.ps1 did not produce ThorNativeFG-debug.apk"}
Copy-Item -LiteralPath $built -Destination $releaseApk -Force

$bytes=(Get-Item $releaseApk).Length
$hash=(Get-FileHash -LiteralPath $releaseApk -Algorithm SHA256).Hash.ToLowerInvariant()
$tag="thorfg-v$VersionName"
$url="https://github.com/dodangquang/thor-native-fg/releases/download/$tag/$apkName"

$payload=[ordered]@{
    schemaVersion=1
    packageName='com.thor.nativefg'
    channel='full'
    versionCode=$VersionCode
    versionName=$VersionName
    minSdk=29
    bytes=$bytes
    sha256=$hash
    apkUrl=$url
    notesVi=$NotesVi
    notesEn=$NotesEn
}
New-Item -ItemType Directory -Force -Path (Split-Path $feed -Parent) | Out-Null
($payload | ConvertTo-Json) | Set-Content -LiteralPath $feed -Encoding UTF8

Write-Host ""
Write-Host "APK : $releaseApk"
Write-Host "Size: $bytes bytes"
Write-Host "SHA : $hash"
Write-Host "Feed: $feed"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  git add AndroidManifest.xml updates/v1/com.thor.nativefg.json"
Write-Host "  git commit -m `"release: $VersionName`""
Write-Host "  git push origin master"
Write-Host "  git tag $tag; git push origin $tag"
Write-Host "  gh release create $tag `"$apkName`" --title `"Thor Native FG $VersionName`""

if($Publish){
    Push-Location $root
    try{
        & git add AndroidManifest.xml updates/v1/com.thor.nativefg.json
        & git commit -m "release: $VersionName"
        & git push origin master
        & git tag $tag
        & git push origin $tag
        if(Get-Command gh -ErrorAction SilentlyContinue){
            & gh release create $tag $apkName --title "Thor Native FG $VersionName" --notes $NotesVi
        }else{
            Write-Warning "gh CLI not found: create the release manually and upload $apkName."
        }
    } finally { Pop-Location }
}
