$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$OutputDirectory = Join-Path $Root "build\host-tests"
$Executable = Join-Path $OutputDirectory "cpu_reference_training_test.exe"

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

& g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic `
    -I (Join-Path $Root "app\src\main\cpp") `
    (Join-Path $Root "app\src\main\cpp\cpu_reference_training.cpp") `
    (Join-Path $Root "host_tests\cpu_reference_training_test.cpp") `
    -o $Executable
if ($LASTEXITCODE -ne 0) {
    throw "Host test compilation failed"
}

& $Executable
if ($LASTEXITCODE -ne 0) {
    throw "Host tests failed"
}
