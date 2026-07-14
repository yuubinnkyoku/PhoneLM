param(
    [string[]]$AdditionalForbiddenText = @()
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "build"
$outputZip = Join-Path $outputDirectory "PhoneLM-QNN-HTP-deviceCreate-support-bundle.zip"

$relativeFiles = @(
    "support/qualcomm-qnn-htp-devicecreate-report.md",
    "support/nubia-qnn-htp-devicecreate-report.md",
    "support/reproduction-files-manifest.md",
    "support/device-properties-anonymized.txt",
    "support/qnn-platform-validator-summary.txt",
    "support/qnn_htp_device_create_repro/README.md",
    "support/qnn_htp_device_create_repro/CMakeLists.txt",
    "support/qnn_htp_device_create_repro/main.cpp",
    "support/qnn_htp_device_create_repro/run.ps1",
    "support/qnn_htp_device_create_repro/result-public.txt",
    "support/vendor_qnn_metadata_probe/README.md",
    "support/vendor_qnn_metadata_probe/CMakeLists.txt",
    "support/vendor_qnn_metadata_probe/main.cpp",
    "support/vendor_qnn_metadata_probe/run.ps1",
    "support/device-build-properties-anonymized.txt",
    "support/vendor-qnn-metadata-results.txt",
    "support/bugreport-qnn-summary.txt",
    "diagnostics/android-linker-qnn-analysis.md",
    "diagnostics/vendor-qnn-version-investigation.md",
    "diagnostics/official-firmware-vendor-qnn-analysis.md"
)

$disallowedEntryExtensions = @(".apk", ".so", ".dll", ".exe", ".raw", ".bin")
$contentPatterns = [ordered]@{
    windows_user_path = '(?i)[A-Z]:[\\/]Users[\\/]'
    windows_absolute_path = '(?i)(?<![A-Za-z0-9_])[A-Z]:[\\/]'
    unix_user_path = '(?i)(?<![A-Za-z0-9_])/(?:home|Users)/[^/\s]+'
    ip_address = '(?<![\d.])(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|1?\d?\d)(?::\d{1,5})?(?![\d.])'
    adb_service_serial = '(?i)\badb-[A-Za-z0-9]{8,}(?:-[A-Za-z0-9_-]+)?'
    emulator_serial = '(?i)\bemulator-\d+'
    full_logcat_header = '(?m)^--------- beginning of '
    full_logcat_line = '(?m)^\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3}\s+'
}

$resolvedRoot = [IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/')
$resolvedBuild = [IO.Path]::GetFullPath($outputDirectory).TrimEnd('\', '/')
$resolvedOutput = [IO.Path]::GetFullPath($outputZip)
if (-not $resolvedBuild.StartsWith($resolvedRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    -not $resolvedOutput.StartsWith($resolvedBuild + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to create or replace an archive outside the repository build directory."
}

$violations = [Collections.Generic.List[string]]::new()
$sourceFiles = @()
foreach ($relative in $relativeFiles) {
    $normalizedRelative = $relative.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $source = [IO.Path]::GetFullPath((Join-Path $repoRoot $normalizedRelative))
    if (-not $source.StartsWith($resolvedRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        $violations.Add("Path escapes repository: $relative")
        continue
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        $violations.Add("Missing required file: $relative")
        continue
    }
    $extension = [IO.Path]::GetExtension($relative).ToLowerInvariant()
    if ($disallowedEntryExtensions -contains $extension) {
        $violations.Add("Disallowed archive entry extension: $relative")
    }
    if ($relative -match '(?i)(full[-_.]?logcat|logcat[-_.]?full|full\.log)') {
        $violations.Add("Possible full Logcat entry: $relative")
    }

    $content = [IO.File]::ReadAllText($source)
    foreach ($item in $contentPatterns.GetEnumerator()) {
        if ($content -match $item.Value) {
            $violations.Add("$($item.Key) detected in $relative")
        }
    }
    if ($env:USERNAME -and $env:USERNAME.Length -ge 3 -and
        $content.IndexOf($env:USERNAME, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        $violations.Add("Current Windows username detected in $relative")
    }
    foreach ($forbidden in $AdditionalForbiddenText) {
        if ($forbidden -and $content.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $violations.Add("Additional forbidden text detected in $relative")
        }
    }
    $sourceFiles += [pscustomobject]@{ Relative = $relative; Source = $source }
}

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ }
    throw "Support bundle safety checks failed."
}

[IO.Directory]::CreateDirectory($resolvedBuild) | Out-Null
if ([IO.File]::Exists($resolvedOutput)) {
    [IO.File]::Delete($resolvedOutput)
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($resolvedOutput, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $sourceFiles) {
        $entryName = $file.Relative.Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive,
            $file.Source,
            $entryName,
            [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $archive.Dispose()
}

$verificationArchive = [IO.Compression.ZipFile]::OpenRead($resolvedOutput)
try {
    $entryNames = @($verificationArchive.Entries | ForEach-Object { $_.FullName })
} finally {
    $verificationArchive.Dispose()
}
foreach ($entry in $entryNames) {
    if ($disallowedEntryExtensions -contains [IO.Path]::GetExtension($entry).ToLowerInvariant()) {
        throw "Post-create verification found a disallowed entry: $entry"
    }
}

Write-Output "bundle_path=$resolvedOutput"
Write-Output "safety_check=PASS"
Write-Output "bundle_entries:"
$entryNames
