param(
    [string]$SdkRoot = "D:\Android\Sdk",
    [string]$JavaHome = $env:JAVA_HOME
)

$ErrorActionPreference = "Stop"
$JdkSearchRoot = "D:\Android\jdk-17"
if (-not $JavaHome -and (Test-Path -LiteralPath $JdkSearchRoot)) {
    $JavaHome = Get-ChildItem -LiteralPath $JdkSearchRoot -Directory |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "bin\java.exe") } |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $JavaHome -or -not (Test-Path -LiteralPath (Join-Path $JavaHome "bin\java.exe"))) {
    throw "JDK 17 was not found. Pass -JavaHome or run scripts/setup_android_env.ps1 first."
}
$env:JAVA_HOME = $JavaHome
$env:Path = "$JavaHome\bin;$env:Path"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$AndroidRoot = Join-Path $RepoRoot "android"
$ApkPath = Join-Path $AndroidRoot "app\build\outputs\apk\release\app-release.apk"
$BuildTools = Join-Path $SdkRoot "build-tools\35.0.0"
$Aapt = Join-Path $BuildTools "aapt.exe"
$ApkSigner = Join-Path $BuildTools "apksigner.bat"
$ZipAlign = Join-Path $BuildTools "zipalign.exe"

foreach ($tool in @($Aapt, $ApkSigner, $ZipAlign)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Missing Android Build Tools 35.0.0 component: $tool"
    }
}

Push-Location $AndroidRoot
try {
    & .\gradlew.bat :app:assembleRelease
    if ($LASTEXITCODE -ne 0) { throw "Gradle release build failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $ApkPath)) {
    throw "Release APK was not created: $ApkPath"
}

$Badging = & $Aapt dump badging $ApkPath
$BadgingText = $Badging -join "`n"
if ($LASTEXITCODE -ne 0 -or $BadgingText -notmatch "package: name='com\.espmusicbox\.android'") {
    throw "APK manifest/package validation failed"
}

& $ApkSigner verify --verbose $ApkPath
if ($LASTEXITCODE -ne 0) { throw "APK signature verification failed" }

& $ZipAlign -c -P 16 4 $ApkPath
if ($LASTEXITCODE -ne 0) { throw "APK 16 KiB alignment verification failed" }

$VersionMatch = [regex]::Match($BadgingText, "versionName='([^']+)'" )
if (-not $VersionMatch.Success) { throw "Could not read versionName from APK" }
$VersionName = $VersionMatch.Groups[1].Value

$DistRoot = Join-Path $RepoRoot "dist"
New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
$DistApk = Join-Path $DistRoot "ESPMusicBox-Android-v$VersionName.apk"
Copy-Item -LiteralPath $ApkPath -Destination $DistApk -Force

$Hash = (Get-FileHash -LiteralPath $DistApk -Algorithm SHA256).Hash.ToLowerInvariant()
$HashPath = "$DistApk.sha256"
Set-Content -LiteralPath $HashPath -Encoding ascii -NoNewline -Value "$Hash  $(Split-Path -Leaf $DistApk)"

Write-Host "Release APK: $DistApk"
Write-Host "SHA-256:    $Hash"
