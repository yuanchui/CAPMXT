# Serial Terminal 顺序编译脚本
# 用法:
#   .\build\build-all.ps1
#   .\build\build-all.ps1 -Package
#   .\build\build-all.ps1 -SkipMxtApp
#   .\build\build-all.ps1 -Package -PackageTarget user:nsis:small

param(
    [switch]$Package,
    [switch]$SkipMxtApp,
    [string]$PackageTarget = 'user:nsis:fast',
    [switch]$NoStrictCli
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$nodeArgs = @('build/build-all.js')
if ($Package) { $nodeArgs += '--package' }
if ($SkipMxtApp) { $nodeArgs += '--skip-mxt' }
if ($PackageTarget -and $PackageTarget -ne 'user:nsis:fast') {
    $nodeArgs += "--package-target=$PackageTarget"
}
if ($NoStrictCli) { $nodeArgs += '--no-strict-cli' }

Write-Host "[build-all.ps1] 启动: node $($nodeArgs -join ' ')" -ForegroundColor Cyan
node @nodeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
