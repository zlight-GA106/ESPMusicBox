# Android build env one-shot setup script (Windows PowerShell 5.1)
# Purpose: install Android SDK (cmdline-tools + platform 35 + build-tools 35) and a
#          Temurin JDK 17 under D:\Android ONCE, so that `android\gradlew.bat
#          assembleDebug` works out of the box.
# Why JDK 17 and not Studio jbr: Android Studio 2026.x bundles JDK 25 (jbr), and
#          Gradle 8.9 / AGP 8.7's embedded Kotlin cannot parse "25.0.2"
#          (IllegalArgumentException: 25.0.2). JDK 17 LTS is the safe target.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/setup_android_env.ps1
$ErrorActionPreference = "Stop"

$Jbr = "D:\Android\Android Studio\jbr"
$Jdk17Root = "D:\Android\jdk-17"
$SdkRoot = "D:\Android\Sdk"

# ---------------------------------------------------------------- JDK 17
$Jdk17Home = $null
if (Test-Path $Jdk17Root) {
    $found = Get-ChildItem $Jdk17Root -Directory -ErrorAction SilentlyContinue `
        | Where-Object { Test-Path (Join-Path $_.FullName "bin\java.exe") } | Select-Object -First 1
    if ($found) { $Jdk17Home = $found.FullName }
}
if (-not $Jdk17Home) {
    Write-Host "[1/4] Downloading Temurin JDK 17 ..."
    $zip = Join-Path $env:TEMP "temurin17.zip"
    Invoke-WebRequest -Uri "https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jdk/hotspot/normal/eclipse" `
        -OutFile $zip -UseBasicParsing
    Write-Host "[2/4] Extracting to $Jdk17Root ..."
    New-Item -ItemType Directory -Force -Path $Jdk17Root | Out-Null
    Expand-Archive -Path $zip -DestinationPath $Jdk17Root -Force
    $Jdk17Home = (Get-ChildItem $Jdk17Root -Directory | Where-Object { Test-Path (Join-Path $_.FullName "bin\java.exe") } | Select-Object -First 1).FullName
} else {
    Write-Host "[1/4] JDK 17 already present: $Jdk17Home"
}
Write-Host "JDK17: $Jdk17Home"

$env:JAVA_HOME = $Jdk17Home
$env:Path = "$Jdk17Home\bin;" + $env:Path

# ---------------------------------------------------------------- cmdline-tools
$CmdlineZipUrl = "https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip"
$Temp = Join-Path $env:TEMP "android-cmdline-tools.zip"
New-Item -ItemType Directory -Force -Path $SdkRoot | Out-Null

if (-not (Test-Path (Join-Path $SdkRoot "cmdline-tools\latest\bin\sdkmanager.bat"))) {
    Write-Host "[2/4] Downloading cmdline-tools ..."
    Invoke-WebRequest -Uri $CmdlineZipUrl -OutFile $Temp -UseBasicParsing
    Write-Host "        Extracting ..."
    $Extract = Join-Path $SdkRoot "cmdline-tools-extract"
    if (Test-Path $Extract) { Remove-Item -Recurse -Force $Extract }
    Expand-Archive -Path $Temp -DestinationPath $Extract -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $SdkRoot "cmdline-tools") | Out-Null
    if (Test-Path (Join-Path $SdkRoot "cmdline-tools\latest")) {
        Remove-Item -Recurse -Force (Join-Path $SdkRoot "cmdline-tools\latest")
    }
    Move-Item -Path (Join-Path $Extract "cmdline-tools") -Destination (Join-Path $SdkRoot "cmdline-tools\latest")
    Remove-Item -Recurse -Force $Extract
} else {
    Write-Host "[2/4] cmdline-tools already present, skip download"
}

# ---------------------------------------------------------------- sdkmanager
$SdkManager = Join-Path $SdkRoot "cmdline-tools\latest\bin\sdkmanager.bat"
Write-Host "[3/4] Accepting licenses ..."
1..20 | ForEach-Object { "y" } | & $SdkManager --sdk_root=$SdkRoot --licenses
Write-Host "        licenses exit=$LASTEXITCODE"

Write-Host "[4/4] Installing platform-tools / platforms;android-35 / build-tools;35.0.0 ..."
& $SdkManager --sdk_root=$SdkRoot "platform-tools" "platforms;android-35" "build-tools;35.0.0"
if ($LASTEXITCODE -ne 0) { throw "sdkmanager install failed, exit=$LASTEXITCODE" }

# ---------------------------------------------------------------- persist
setx JAVA_HOME $Jdk17Home | Out-Null
Write-Host "Done. JAVA_HOME=$Jdk17Home (user env persisted)"
Write-Host "SDK at: $SdkRoot"
