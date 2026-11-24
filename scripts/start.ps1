# Start script for Smart Data Center OS Simulator
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$EngineBin = Join-Path $Root "bin\dc_sim.exe"

if (-not (Test-Path $EngineBin)) {
    Write-Host "Engine binary not found. Building first..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "build.ps1")
}

Write-Host "Starting Smart Data Center OS Simulator server..." -ForegroundColor Cyan
node (Join-Path $Root "server\server.js")
