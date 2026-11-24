# Build script for Smart Data Center OS Simulator (Windows / MinGW g++)
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$BinDir = Join-Path $Root "bin"
New-Item -ItemType Directory -Force -Path $BinDir | Out-Null

$CoreSources = @(
    "src/core/Json.cpp",
    "src/core/Types.cpp",
    "src/process/Process.cpp",
    "src/analytics/Metrics.cpp",
    "src/cpu/CpuScheduler.cpp",
    "src/cpu/CpuSimulator.cpp",
    "src/memory/MemoryManager.cpp",
    "src/memory/MemorySimulator.cpp",
    "src/disk/DiskScheduler.cpp",
    "src/disk/DiskSimulator.cpp",
    "src/synchronization/Threading.cpp",
    "src/synchronization/ConcurrencyDemo.cpp",
    "src/synchronization/ResourceManager.cpp",
    "src/simulation/SimulationEngine.cpp"
) | ForEach-Object { Join-Path $Root $_ }

$IncDir = Join-Path $Root "include"
$EngineOut = Join-Path $BinDir "dc_sim.exe"
$TestOut = Join-Path $BinDir "dc_tests.exe"

Write-Host "[1/2] Compiling C++ simulation engine -> $EngineOut" -ForegroundColor Cyan
& g++ -std=c++14 -O2 -Wall -Wextra "-I$IncDir" @CoreSources (Join-Path $Root "src/main.cpp") -o $EngineOut
if ($LASTEXITCODE -ne 0) { throw "Engine build failed." }

Write-Host "[2/2] Compiling C++ unit & integration tests -> $TestOut" -ForegroundColor Cyan
& g++ -std=c++14 -O2 -Wall -Wextra "-I$IncDir" @CoreSources (Join-Path $Root "tests/test_all.cpp") -o $TestOut
if ($LASTEXITCODE -ne 0) { throw "Test build failed." }

Write-Host "Build succeeded!" -ForegroundColor Green
