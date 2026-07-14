param(
    [Parameter(Mandatory = $true)][string]$SdkRoot,
    [Parameter(Mandatory = $true)][string]$DeviceSerial,
    [Parameter(Mandatory = $true)][string]$LibraryPath,
    [string]$NdkRoot = ""
)

$ErrorActionPreference = "Stop"
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildDir = Join-Path $repoRoot "build\vendor_qnn_probe\android-arm64"
$logPath = Join-Path $repoRoot "diagnostics\local\vendor-qnn-direct-load-probe.log"
$remoteRoot = "/data/local/tmp/phonelm-vendor-qnn-metadata-probe"

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Find-AndroidSdk {
    $candidates = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)
    if ($env:LOCALAPPDATA) { $candidates += (Join-Path $env:LOCALAPPDATA "Android\Sdk") }
    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Android SDK not found. Set ANDROID_HOME or ANDROID_SDK_ROOT."
}

if (-not $LibraryPath.StartsWith('/')) { throw "LibraryPath must be an absolute Android path." }
$sdkRootResolved = (Resolve-Path -LiteralPath $SdkRoot).Path
$null = Require-File (Join-Path $sdkRootResolved "include\QNN\QnnInterface.h") "QNN interface header"
$androidSdk = Find-AndroidSdk
$adb = Require-File (Join-Path $androidSdk "platform-tools\adb.exe") "adb"
$cmake = Require-File (Join-Path $androidSdk "cmake\3.22.1\bin\cmake.exe") "CMake"
$ninja = Require-File (Join-Path $androidSdk "cmake\3.22.1\bin\ninja.exe") "Ninja"
if (-not $NdkRoot) {
    $NdkRoot = if ($env:ANDROID_NDK_ROOT) { $env:ANDROID_NDK_ROOT } else { Join-Path $androidSdk "ndk\26.2.11394342" }
}
$toolchain = Require-File (Join-Path (Resolve-Path -LiteralPath $NdkRoot).Path "build\cmake\android.toolchain.cmake") "NDK toolchain"

$devices = (& $adb devices -l) -join "`n"
if ($devices -notmatch "(?m)^$([regex]::Escape($DeviceSerial))\s+device\b.*\bmodel:NX741J\b") {
    throw "The requested online NX741J was not found."
}

[IO.Directory]::CreateDirectory($buildDir) | Out-Null
[IO.Directory]::CreateDirectory((Split-Path -Parent $logPath)) | Out-Null
& $cmake -S $PSScriptRoot -B $buildDir -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DANDROID_ABI=arm64-v8a" "-DANDROID_PLATFORM=android-26" `
    "-DANDROID_STL=c++_static" "-DQAIRT_SDK_ROOT=$sdkRootResolved"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
& $cmake --build $buildDir --target vendor_qnn_metadata_probe
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }

$executable = Require-File (Join-Path $buildDir "vendor_qnn_metadata_probe") "probe executable"
& $adb -s $DeviceSerial shell "mkdir -p $remoteRoot"
& $adb -s $DeviceSerial push $executable "$remoteRoot/"
if ($LASTEXITCODE -ne 0) { throw "Failed to deploy probe" }
& $adb -s $DeviceSerial shell "chmod 755 $remoteRoot/vendor_qnn_metadata_probe"

$identity = (& $adb -s $DeviceSerial shell id) -join "`n"
$output = (& $adb -s $DeviceSerial shell "cd $remoteRoot && ./vendor_qnn_metadata_probe '$LibraryPath'" 2>&1) -join "`n"
$exitCode = $LASTEXITCODE
$record = @"
device_serial=$DeviceSerial
shell_identity=$identity
$output
probe_exit_code=$exitCode
"@
[IO.File]::AppendAllText($logPath, $record + "`n---`n", [Text.UTF8Encoding]::new($false))
Write-Output $output
Write-Output "probe_exit_code=$exitCode"
Write-Output "local_log=$logPath"
