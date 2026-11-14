param(
    [string]$Edition = "full",
    [string]$Device = "",
    [int]$Days = 0,
    [string]$Note = "",
    [string]$PrivateKey = "private\thorfg-license-private.xml",
    [string]$OutFile = "" # empty = keys\THORFG-<edition>-<device|any>-<timestamp>.txt
)
# Issues a Thor Native FG licence key.
#
#   THORFG1.<payload-base64url>.<signature-base64url>
#
# The payload is signed with the RSA key whose public half is pinned in
# assets/thorfg-license.json. The private key never enters the repository
# (private/ is git-ignored); keep a backup of it, because a new key pair makes
# every previously issued licence unverifiable.
#
# Examples:
#   .\tools\license-issue.ps1 -Device 1A2B3C4D5E6F7788 -Note "Thor cua Quang"
#   .\tools\license-issue.ps1 -Days 365 -Note "ban 1 nam"
#   .\tools\license-issue.ps1 -Edition full -Note "perpetual, any device"
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$keyPath=Join-Path $root $PrivateKey
if(-not (Test-Path -LiteralPath $keyPath)){throw "Private key not found: $keyPath"}

$device=($Device -replace '[^0-9A-Fa-f]','').ToLowerInvariant()
if($device -ne '' -and $device.Length -ne 16){throw "Device id must be 16 hex characters (see the app's Ban quyen dialog)."}
$issued=[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$expires=if($Days -gt 0){$issued + [int64]$Days*86400}else{0}

$payload=[ordered]@{ pkg='com.thor.nativefg'; dev=$device; ed=$Edition; exp=$expires; i=$issued; n=$Note }
$json=($payload | ConvertTo-Json -Compress)
$payloadBytes=[Text.Encoding]::UTF8.GetBytes($json)
$payloadB64=[Convert]::ToBase64String($payloadBytes).Replace('+','-').Replace('/','_').TrimEnd('=')

$rsa=[System.Security.Cryptography.RSA]::Create()
try{
    $rsa.FromXmlString([IO.File]::ReadAllText($keyPath))
    $signature=$rsa.SignData([Text.Encoding]::ASCII.GetBytes($payloadB64),[System.Security.Cryptography.HashAlgorithmName]::SHA256,[System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
}finally{$rsa.Dispose()}
$sigB64=[Convert]::ToBase64String($signature).Replace('+','-').Replace('/','_').TrimEnd('=')

$key="THORFG1.$payloadB64.$sigB64"
$summary=@"
Edition : $Edition
Device  : $(if($device -eq ''){'any (không gắn thiết bị)'}else{$device})
Expires : $(if($expires -eq 0){'vĩnh viễn'}else{[DateTimeOffset]::FromUnixTimeSeconds($expires).ToLocalTime().ToString('yyyy-MM-dd HH:mm')})
Note    : $Note

$key
"@
$autoName="THORFG-$Edition-$(if($device -eq '' ){'any'}else{$device})-$(Get-Date -Format yyyyMMdd-HHmmss).txt"
if($OutFile -eq ''){$OutFile=Join-Path 'keys' $autoName}
if($OutFile -ne ''){
    $path=if([IO.Path]::IsPathRooted($OutFile)){$OutFile}else{Join-Path $root $OutFile}
    New-Item -ItemType Directory -Force -Path (Split-Path $path -Parent) | Out-Null
    [IO.File]::WriteAllText($path,$key,[Text.UTF8Encoding]::new($false))
    Write-Host "Key written to $path"
}
Write-Host $summary
