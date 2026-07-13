param(
    [Parameter(Position = 0)]
    [string]$SdkRoot = ""
)

$ErrorActionPreference = "Stop"
$RepositoryRoot = Split-Path -Parent $PSScriptRoot

function Write-Result([string]$Key, [object]$Value) {
    $text = if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) {
        "NONE"
    } else {
        ([string]$Value -replace "[\r\n]+", " " -replace "\s+", " ").Trim()
    }
    Write-Output "$Key=$text"
}

function Join-Paths($Items) {
    $paths = @($Items | ForEach-Object {
        if ($_ -is [System.IO.FileSystemInfo]) { $_.FullName } else { [string]$_ }
    } | Where-Object { $_ } | Sort-Object -Unique)
    if ($paths.Count -eq 0) { return "NONE" }
    return $paths -join ";"
}

function Convert-GradlePath([string]$Value) {
    $result = $Value.Trim()
    $result = $result -replace '\\:', ':'
    $result = $result -replace '\\\\', '\'
    return $result
}

function Get-PropertyCandidates {
    $propertyFiles = @(
        (Join-Path $RepositoryRoot "local.properties"),
        (Join-Path $RepositoryRoot "gradle.properties"),
        (Join-Path $HOME ".gradle\gradle.properties")
    )
    foreach ($file in $propertyFiles) {
        if (-not (Test-Path -LiteralPath $file)) { continue }
        foreach ($line in Get-Content -LiteralPath $file -ErrorAction SilentlyContinue) {
            if ($line -match '^\s*qairt\.sdkRoot\s*=\s*(.+?)\s*$') {
                [pscustomobject]@{ Path = (Convert-GradlePath $Matches[1]); Source = $file }
            }
        }
    }
}

function Test-Aarch64Elf([System.IO.FileInfo]$File) {
    try {
        $stream = [System.IO.File]::OpenRead($File.FullName)
        try {
            $bytes = New-Object byte[] 20
            if ($stream.Read($bytes, 0, $bytes.Length) -ne $bytes.Length) { return $false }
            if ($bytes[0] -ne 0x7f -or $bytes[1] -ne 0x45 -or
                $bytes[2] -ne 0x4c -or $bytes[3] -ne 0x46) { return $false }
            $machine = if ($bytes[5] -eq 2) {
                ($bytes[18] -shl 8) -bor $bytes[19]
            } else {
                $bytes[18] -bor ($bytes[19] -shl 8)
            }
            return $machine -eq 183
        } finally {
            $stream.Dispose()
        }
    } catch {
        return $false
    }
}

function Get-VersionTriplets($HeaderFiles, [string]$PrefixPattern) {
    $macros = @{}
    foreach ($file in $HeaderFiles) {
        foreach ($line in Get-Content -LiteralPath $file.FullName -ErrorAction SilentlyContinue) {
            if ($line -match '^\s*#\s*define\s+([A-Za-z0-9_]*VERSION)_(MAJOR|MINOR|PATCH)\s+\(?([0-9]+)') {
                $prefix = $Matches[1]
                $part = $Matches[2]
                $value = $Matches[3]
                if ($prefix -notmatch $PrefixPattern) { continue }
                if (-not $macros.ContainsKey($prefix)) { $macros[$prefix] = @{} }
                $macros[$prefix][$part] = $value
            }
        }
    }
    $versions = foreach ($prefix in $macros.Keys) {
        $parts = $macros[$prefix]
        if ($parts.ContainsKey("MAJOR") -and $parts.ContainsKey("MINOR")) {
            $version = "$($parts['MAJOR']).$($parts['MINOR'])"
            if ($parts.ContainsKey("PATCH")) { $version += ".$($parts['PATCH'])" }
            "${prefix}:$version"
        }
    }
    return @($versions | Sort-Object -Unique)
}

$candidates = @()
if (-not [string]::IsNullOrWhiteSpace($SdkRoot)) {
    $candidates += [pscustomobject]@{ Path = $SdkRoot; Source = "argument" }
}
if (-not [string]::IsNullOrWhiteSpace($env:QAIRT_SDK_ROOT)) {
    $candidates += [pscustomobject]@{ Path = $env:QAIRT_SDK_ROOT; Source = "QAIRT_SDK_ROOT" }
}
$candidates += @(Get-PropertyCandidates)

$commonRoots = @(
    "C:\Qualcomm",
    "C:\Program Files\Qualcomm",
    "C:\Program Files (x86)\Qualcomm",
    (Join-Path $HOME "Qualcomm"),
    (Join-Path $HOME "qairt"),
    (Join-Path $HOME ".qairt"),
    (Join-Path $env:LOCALAPPDATA "Qualcomm")
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
foreach ($commonRoot in $commonRoots) {
    $header = Get-ChildItem -LiteralPath $commonRoot -Recurse -File -Filter "QnnInterface.h" `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($header) {
        $cursor = $header.Directory
        while ($cursor.Parent -and $cursor.Parent.FullName.StartsWith($commonRoot,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            if (Test-Path -LiteralPath (Join-Path $cursor.FullName "include")) { break }
            $cursor = $cursor.Parent
        }
        $candidates += [pscustomobject]@{ Path = $cursor.FullName; Source = "common_path_scan" }
    }
}

$candidates = @($candidates | Where-Object { $_.Path } |
    Group-Object { [System.IO.Path]::GetFullPath($_.Path) } | ForEach-Object { $_.Group[0] })
$selected = $candidates | Where-Object { Test-Path -LiteralPath $_.Path -PathType Container } |
    Where-Object {
        Get-ChildItem -LiteralPath $_.Path -Recurse -File -Filter "QnnInterface.h" `
            -ErrorAction SilentlyContinue | Select-Object -First 1
    } | Select-Object -First 1
if (-not $selected) { $selected = $candidates | Select-Object -First 1 }

Write-Result "check" "QAIRT_SDK_INVENTORY"
Write-Result "requested_sdk_root" $SdkRoot
Write-Result "candidate_sources" (($candidates | ForEach-Object { "$($_.Source):$($_.Path)" }) -join ";")

if (-not $selected -or -not (Test-Path -LiteralPath $selected.Path -PathType Container)) {
    Write-Result "sdk_root_exists" "false"
    Write-Result "qnn_interface_header_exists" "false"
    Write-Result "qnn_types_header_exists" "false"
    Write-Result "qnn_implementation_ready" "false"
    Write-Result "status" "BLOCKED_BY_QAIRT_SDK_NOT_INSTALLED"
    exit 2
}

$resolvedRoot = (Resolve-Path -LiteralPath $selected.Path).Path
$allFiles = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Force `
    -ErrorAction SilentlyContinue)
$interfaceHeaders = @($allFiles | Where-Object { $_.Name -ceq "QnnInterface.h" })
$typesHeaders = @($allFiles | Where-Object { $_.Name -ceq "QnnTypes.h" })
$qnnHeaders = @($allFiles | Where-Object {
    $_.Extension -eq ".h" -and $_.Name -match '^(Qnn|Qairt)'
})
$apiVersions = @(Get-VersionTriplets $qnnHeaders '(?i)(QNN|QAIRT).*(API|INTERFACE)|QNN')
$sdkVersions = @(Get-VersionTriplets $qnnHeaders '(?i)(QAIRT|QNN).*SDK')
if ($sdkVersions.Count -eq 0 -and (Split-Path -Leaf $resolvedRoot) -match '(\d+\.\d+(?:\.\d+)?)') {
    $sdkVersions = @("root_directory:$($Matches[1])")
}

$sharedObjects = @($allFiles | Where-Object { $_.Extension -eq ".so" })
$aarch64Objects = @($sharedObjects | Where-Object { Test-Aarch64Elf $_ })
$androidArm64Objects = @($aarch64Objects | Where-Object {
    $_.FullName -match '(?i)(android.*(?:aarch64|arm64)|(?:aarch64|arm64).*android)'
})
$cpuBackendCandidates = @($androidArm64Objects | Where-Object {
    $_.FullName -match '(?i)(qnn|qairt).*cpu|cpu.*(qnn|qairt)'
})
$htpBackendCandidates = @($androidArm64Objects | Where-Object {
    $_.FullName -match '(?i)(qnn|qairt).*htp|htp.*(qnn|qairt)'
})
$htpPrepareCandidates = @($sharedObjects | Where-Object {
    $_.FullName -match '(?i)(htp.*(prepare|prep)|(prepare|prep).*htp)'
})
$dspSkelCandidates = @($sharedObjects | Where-Object {
    $_.FullName -match '(?i)(dsp|hexagon|htp)' -and $_.Name -match '(?i)skel'
})
$dspStubCandidates = @($sharedObjects | Where-Object {
    $_.FullName -match '(?i)(dsp|hexagon|htp)' -and $_.Name -match '(?i)stub'
})
$netRunTools = @($allFiles | Where-Object { $_.Name -match '^qnn-net-run(?:\..*)?$' })
$validatorTools = @($allFiles | Where-Object {
    $_.Name -match '^qnn-platform-validator(?:\..*)?$'
})

$sampleFiles = @($allFiles | Where-Object {
    $_.FullName -match '(?i)[\\/](sample|samples|example|examples)[\\/]' -and
    $_.Extension -match '^\.(c|cc|cpp|cxx|h|hpp|cmake|txt|md|json|xml)$'
})
$sampleEvidence = @()
$cpuSampleEvidence = @()
$htpSampleEvidence = @()
foreach ($file in $sampleFiles) {
    if ($file.Length -gt 5MB) { continue }
    if (Select-String -LiteralPath $file.FullName -Pattern 'QnnInterface\.h' -Quiet `
            -ErrorAction SilentlyContinue) {
        $sampleEvidence += $file
        if ($file.FullName -match '(?i)cpu' -or
            (Select-String -LiteralPath $file.FullName -Pattern '(?i)\bCPU\b|QnnCpu' `
                -Quiet -ErrorAction SilentlyContinue)) {
            $cpuSampleEvidence += $file
        }
        if ($file.FullName -match '(?i)htp' -or
            (Select-String -LiteralPath $file.FullName -Pattern '(?i)\bHTP\b|QnnHtp' `
                -Quiet -ErrorAction SilentlyContinue)) {
            $htpSampleEvidence += $file
        }
    }
}

Write-Result "sdk_root_exists" "true"
Write-Result "sdk_root" $resolvedRoot
Write-Result "sdk_root_source" $selected.Source
Write-Result "qnn_interface_header_exists" ($interfaceHeaders.Count -gt 0).ToString().ToLowerInvariant()
Write-Result "qnn_interface_headers" (Join-Paths $interfaceHeaders)
Write-Result "qnn_types_header_exists" ($typesHeaders.Count -gt 0).ToString().ToLowerInvariant()
Write-Result "qnn_types_headers" (Join-Paths $typesHeaders)
Write-Result "include_directories" (Join-Paths (($interfaceHeaders + $typesHeaders) | ForEach-Object { $_.Directory }))
Write-Result "qairt_sdk_version" $(if ($sdkVersions.Count) { $sdkVersions -join ";" } else { "UNDETERMINED" })
Write-Result "qnn_api_version" $(if ($apiVersions.Count) { $apiVersions -join ";" } else { "UNDETERMINED" })
Write-Result "android_arm64_library_directories" (Join-Paths ($androidArm64Objects | ForEach-Object { $_.Directory }))
Write-Result "android_arm64_libraries" (Join-Paths $androidArm64Objects)
Write-Result "cpu_backend_library_candidates" (Join-Paths $cpuBackendCandidates)
Write-Result "htp_backend_library_candidates" (Join-Paths $htpBackendCandidates)
Write-Result "htp_runtime_library_directories" (Join-Paths ($htpBackendCandidates | ForEach-Object { $_.Directory }))
Write-Result "htp_prepare_or_equivalent_candidates" (Join-Paths $htpPrepareCandidates)
Write-Result "dsp_skel_candidates" (Join-Paths $dspSkelCandidates)
Write-Result "dsp_stub_candidates" (Join-Paths $dspStubCandidates)
Write-Result "dsp_library_directories" (Join-Paths (($dspSkelCandidates + $dspStubCandidates) | ForEach-Object { $_.Directory }))
Write-Result "qnn_net_run" (Join-Paths $netRunTools)
Write-Result "qnn_platform_validator" (Join-Paths $validatorTools)
Write-Result "official_sample_candidates" (Join-Paths $sampleEvidence)
Write-Result "official_sample_directories" (Join-Paths ($sampleEvidence | ForEach-Object { $_.Directory }))
Write-Result "cpu_sample_candidates" (Join-Paths $cpuSampleEvidence)
Write-Result "htp_sample_candidates" (Join-Paths $htpSampleEvidence)
Write-Result "classification_note" "Candidate roles are inferred from installed paths/names and must be confirmed against that SDK's official build files; no library basename is hard-coded."
Write-Result "qnn_implementation_ready" "false"

$inventoryComplete = $interfaceHeaders.Count -gt 0 -and $typesHeaders.Count -gt 0 -and
    $sdkVersions.Count -gt 0 -and $apiVersions.Count -gt 0 -and
    $androidArm64Objects.Count -gt 0 -and
    $cpuBackendCandidates.Count -gt 0 -and $htpBackendCandidates.Count -gt 0 -and
    $htpPrepareCandidates.Count -gt 0 -and
    $dspSkelCandidates.Count -gt 0 -and $dspStubCandidates.Count -gt 0 -and
    $netRunTools.Count -gt 0 -and $validatorTools.Count -gt 0 -and
    $sampleEvidence.Count -gt 0 -and $cpuSampleEvidence.Count -gt 0 -and
    $htpSampleEvidence.Count -gt 0
if ($inventoryComplete) {
    Write-Result "status" "QAIRT_SDK_INVENTORY_COMPLETE_ADAPTER_NOT_IMPLEMENTED"
    exit 0
}

Write-Result "status" "QAIRT_SDK_FOUND_INVENTORY_INCOMPLETE"
exit 3
