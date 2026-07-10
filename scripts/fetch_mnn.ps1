param(
    [string]$Target = ""
)

$ErrorActionPreference = "Stop"
$Repository = "https://github.com/alibaba/MNN.git"
$Tag = "3.5.0"
$Commit = "c35f14f3ab5cb65094863b9a0e888370b027a670"
$Root = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($Target)) {
    $Target = Join-Path $Root "third_party\MNN"
}

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git command failed: git $($Arguments -join ' ')"
    }
}

if (Test-Path (Join-Path $Target ".git")) {
    $Current = (& git -C $Target rev-parse HEAD 2>$null).Trim()
    if ($LASTEXITCODE -eq 0 -and $Current -eq $Commit) {
        Write-Host "MNN is already pinned to $Commit"
        exit 0
    }
    throw "Existing checkout at '$Target' is not the pinned commit. Move or remove it explicitly, then rerun."
}

if (Test-Path $Target) {
    $Children = @(Get-ChildItem -Force $Target)
    if ($Children.Count -ne 0) {
        throw "Target '$Target' exists and is not empty."
    }
} else {
    New-Item -ItemType Directory -Force -Path $Target | Out-Null
}

Invoke-Git init $Target
Invoke-Git -C $Target remote add origin $Repository
Invoke-Git -C $Target fetch --depth 1 origin "refs/tags/${Tag}:refs/tags/${Tag}"
Invoke-Git -C $Target checkout --detach $Commit

$Resolved = (& git -C $Target rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $Resolved -ne $Commit) {
    throw "MNN pin verification failed: expected $Commit, got $Resolved"
}

Write-Host "MNN $Tag is ready at $Target ($Resolved)"

