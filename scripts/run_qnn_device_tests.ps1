param(
    [Parameter(Mandatory = $true)][string]$SdkRoot,
    [Parameter(Mandatory = $true)][string]$DeviceSerial
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$adb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
$env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA "Android\Sdk"
$env:QAIRT_SDK_ROOT = $SdkRoot
$package = "com.yuubinnkyoku.phonelm"
$activity = "$package/.MainActivity"

function Invoke-Adb([string[]]$Arguments) {
    & $adb -s $DeviceSerial @Arguments
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $($Arguments -join ' ')" }
}
function Run-Mode([string]$Mode, [string]$LogName) {
    Invoke-Adb @("shell", "am", "force-stop", $package)
    Invoke-Adb @("logcat", "-c")
    & $adb -s $DeviceSerial shell run-as $package rm files/device-test-result.txt 2>$null
    Invoke-Adb @("shell", "am", "start", "-W", "-n", $activity, "--es", "phonelm.mode", $Mode)
    $result = ""
    for ($i = 0; $i -lt 180; $i++) {
        Start-Sleep -Milliseconds 500
        $result = (& $adb -s $DeviceSerial shell run-as $package cat files/device-test-result.txt 2>$null) -join "`n"
        if ($result -match "execution_mode=$Mode" -and $result -match "status=") { break }
    }
    $logPath = Join-Path $root $LogName
    (& $adb -s $DeviceSerial logcat -d -b all -v threadtime) | Set-Content -Encoding utf8 $logPath
    Add-Content -Encoding utf8 $logPath "`nDEVICE_RESULT_FILE`n$result"
    $result | Set-Content -Encoding utf8 (Join-Path $root ($LogName -replace '-full\.log$', '-result.txt'))
    return $result
}

Push-Location $root
try {
    $devices = (& $adb devices -l) -join "`n"
    if ($devices -notmatch "(?m)^$([regex]::Escape($DeviceSerial))\s+device\b") {
        throw "Device is not online: $DeviceSerial"
    }
    .\gradlew.bat :app:assembleDebug '-Pphonelm.enableQnn=true' "-Pqairt.sdkRoot=$SdkRoot"
    if ($LASTEXITCODE -ne 0) { throw "QNN APK build failed" }
    Invoke-Adb @("install", "-r", "app\build\outputs\apk\debug\app-debug.apk")
    $results = @{}
    $results.CPU_REFERENCE = Run-Mode "CPU_REFERENCE" "phonelm-cpu-reference-full.log"
    $results.QNN_CPU_FORWARD = Run-Mode "QNN_CPU_FORWARD" "phonelm-qnn-cpu-forward-full.log"
    $results.QNN_HTP_DEVICE_PROBE = Run-Mode "QNN_HTP_DEVICE_PROBE" "phonelm-qnn-htp-device-probe-full.log"
    if ($results.QNN_HTP_DEVICE_PROBE -match '(?m)^status=SUCCESS$') {
        $results.QNN_HTP_FORWARD = Run-Mode "QNN_HTP_FORWARD" "phonelm-qnn-htp-forward-full.log"
        $results.QNN_HTP_FORWARD_DW = Run-Mode "QNN_HTP_FORWARD_DW" "phonelm-qnn-htp-forward-dw-full.log"
    }
    $summary = $results.GetEnumerator() | Sort-Object Name | ForEach-Object {
        $status = if ($_.Value -match '(?m)^status=(.+)$') { $Matches[1] } else { "NO_RESULT" }
        "$($_.Name)=$status"
    }
    $summary | Set-Content -Encoding utf8 qnn-device-test-summary.txt
    $summary
    $allPassed = $results.Values | ForEach-Object { $_ -match '(?m)^status=SUCCESS$' }
    if ($allPassed -contains $false) { exit 1 }
} finally { Pop-Location }
