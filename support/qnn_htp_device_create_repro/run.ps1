param(
    [Parameter(Mandatory = $true)][string]$SdkRoot,
    [Parameter(Mandatory = $true)][string]$DeviceSerial,
    [string]$NdkRoot = ""
)

$ErrorActionPreference = "Stop"
if (Test-Path variable:PSNativeCommandUseErrorActionPreference) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildDir = Join-Path $repoRoot "build\qnn_htp_device_create_repro\android-arm64"
$logPath = Join-Path $repoRoot "diagnostics\local\minimal-device-create-repro.log"
$remoteRoot = "/data/local/tmp/phonelm-qnn-device-create-repro"

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Find-AndroidSdk {
    $candidates = @()
    if ($env:ANDROID_HOME) { $candidates += $env:ANDROID_HOME }
    if ($env:ANDROID_SDK_ROOT) { $candidates += $env:ANDROID_SDK_ROOT }
    if ($env:LOCALAPPDATA) { $candidates += (Join-Path $env:LOCALAPPDATA "Android\Sdk") }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw "Android SDK not found. Set ANDROID_HOME or ANDROID_SDK_ROOT."
}

$sdkRootResolved = (Resolve-Path -LiteralPath $SdkRoot).Path
$qnnInclude = Require-File (Join-Path $sdkRootResolved "include\QNN\QnnInterface.h") "QNN interface header"
$null = $qnnInclude
$htpLibrary = Require-File (Join-Path $sdkRootResolved "lib\aarch64-android\libQnnHtp.so") "HTP backend"
$htpStub = Require-File (Join-Path $sdkRootResolved "lib\aarch64-android\libQnnHtpV81Stub.so") "V81 stub"

$androidSdk = Find-AndroidSdk
$adb = Require-File (Join-Path $androidSdk "platform-tools\adb.exe") "adb"
$cmake = Require-File (Join-Path $androidSdk "cmake\3.22.1\bin\cmake.exe") "CMake 3.22.1"
$ninja = Require-File (Join-Path $androidSdk "cmake\3.22.1\bin\ninja.exe") "Ninja"

if (-not $NdkRoot) {
    if ($env:ANDROID_NDK_ROOT) {
        $NdkRoot = $env:ANDROID_NDK_ROOT
    } else {
        $NdkRoot = Join-Path $androidSdk "ndk\26.2.11394342"
    }
}
$ndkRootResolved = (Resolve-Path -LiteralPath $NdkRoot).Path
$toolchain = Require-File (Join-Path $ndkRootResolved "build\cmake\android.toolchain.cmake") "Android NDK toolchain"

$deviceList = (& $adb devices -l) -join "`n"
if ($deviceList -notmatch "(?m)^$([regex]::Escape($DeviceSerial))\s+device\b.*\bmodel:NX741J\b") {
    throw "The requested online NX741J was not found in adb devices -l."
}

[IO.Directory]::CreateDirectory($buildDir) | Out-Null
[IO.Directory]::CreateDirectory((Split-Path -Parent $logPath)) | Out-Null

& $cmake -S $PSScriptRoot -B $buildDir -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DANDROID_ABI=arm64-v8a" `
    "-DANDROID_PLATFORM=android-26" `
    "-DANDROID_STL=c++_static" `
    "-DQAIRT_SDK_ROOT=$sdkRootResolved"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

& $cmake --build $buildDir --target qnn_htp_device_create_repro
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

$executable = Require-File (Join-Path $buildDir "qnn_htp_device_create_repro") "Android reproduction executable"

& $adb -s $DeviceSerial shell "rm -rf $remoteRoot && mkdir -p $remoteRoot/lib"
if ($LASTEXITCODE -ne 0) { throw "Failed to prepare remote directory" }
& $adb -s $DeviceSerial push $executable "$remoteRoot/"
if ($LASTEXITCODE -ne 0) { throw "Failed to push reproduction executable" }
& $adb -s $DeviceSerial push $htpLibrary "$remoteRoot/lib/"
if ($LASTEXITCODE -ne 0) { throw "Failed to push HTP backend" }
& $adb -s $DeviceSerial push $htpStub "$remoteRoot/lib/"
if ($LASTEXITCODE -ne 0) { throw "Failed to push V81 stub" }
& $adb -s $DeviceSerial shell "chmod 755 $remoteRoot/qnn_htp_device_create_repro"
if ($LASTEXITCODE -ne 0) { throw "Failed to make reproduction executable" }

$identity = (& $adb -s $DeviceSerial shell id) -join "`n"
$enforcement = (& $adb -s $DeviceSerial shell getenforce) -join "`n"
$workingDirectory = (& $adb -s $DeviceSerial shell pwd) -join "`n"
$deployedFiles = (& $adb -s $DeviceSerial shell "find $remoteRoot -maxdepth 2 -type f -exec ls -lZ {} \;") -join "`n"

$remoteCommand = "cd $remoteRoot && env LD_LIBRARY_PATH=$remoteRoot/lib ADSP_LIBRARY_PATH=/vendor/lib/rfsa/adsp ./qnn_htp_device_create_repro"
$executionOutput = (& $adb -s $DeviceSerial shell $remoteCommand 2>&1) -join "`n"
$executionExitCode = $LASTEXITCODE

$log = @"
device_serial=$DeviceSerial
remote_root=$remoteRoot

SHELL_IDENTITY
$identity
SELINUX_ENFORCEMENT
$enforcement
INITIAL_WORKING_DIRECTORY
$workingDirectory

DEPLOYED_FILES
$deployedFiles

EXECUTION_OUTPUT
$executionOutput
execution_exit_code=$executionExitCode
"@
[IO.File]::WriteAllText($logPath, $log, [Text.UTF8Encoding]::new($false))

Write-Output $executionOutput
Write-Output "execution_exit_code=$executionExitCode"
Write-Output "local_log=$logPath"

if ($executionOutput -notmatch "device_create_result_decimal=14001" -or
    $executionOutput -notmatch "device_create_result_hex=0x36b1" -or
    $executionOutput -notmatch "context_create_called=false") {
    throw "The one-shot reproduction did not produce the expected 14001/null-context evidence."
}

Write-Output "reproduction_status=CONFIRMED"
