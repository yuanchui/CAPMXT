# 从国内镜像安装 Electron 二进制（绕过 GitHub 超时）
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$electronPkg = Get-ChildItem -Path 'node_modules\.pnpm' -Filter 'electron@*' -Directory |
  Sort-Object Name -Descending |
  Select-Object -First 1
if (-not $electronPkg) {
  throw '未找到 electron 包，请先运行 pnpm install'
}

$electronDir = Join-Path $electronPkg.FullName 'node_modules\electron'
$distDir = Join-Path $electronDir 'dist'
$pkgJson = Get-Content (Join-Path $electronDir 'package.json') -Raw | ConvertFrom-Json
$version = $pkgJson.version
$exePath = Join-Path $distDir 'electron.exe'

Write-Host "[install-electron] v$version -> $distDir"

if (Test-Path $exePath) {
  $pathTxt = Join-Path $distDir 'path.txt'
  if (-not (Test-Path $pathTxt)) {
    Set-Content -Path $pathTxt -Value $exePath -NoNewline
  }
  Write-Host '[install-electron] 已存在，跳过'
  exit 0
}

# 1) 尝试镜像 postinstall（需同时设环境变量，.npmrc 有时对 pnpm 不生效）
$env:ELECTRON_MIRROR = 'https://npmmirror.com/mirrors/electron/'
Remove-Item Env:ELECTRON_CUSTOM_DIR -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $distDir -ErrorAction SilentlyContinue

Write-Host '[install-electron] 尝试镜像 postinstall ...'
Push-Location $electronDir
try {
  & node install.js
  if (Test-Path $exePath) {
    Write-Host '[install-electron] postinstall 成功'
    exit 0
  }
} catch {
  Write-Host "[install-electron] postinstall 失败: $_"
} finally {
  Pop-Location
}

# 2) 手动从 npmmirror 下载 zip
$zipName = "electron-v$version-win32-x64.zip"
$urls = @(
  "https://npmmirror.com/mirrors/electron/v$version/$zipName",
  "https://cdn.npmmirror.com/binaries/electron/v$version/$zipName"
)

New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$zipPath = Join-Path $env:TEMP $zipName
$downloaded = $false

foreach ($url in $urls) {
  Write-Host "[install-electron] 下载: $url"
  try {
    Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing -TimeoutSec 600
    if ((Get-Item $zipPath).Length -gt 1MB) {
      $downloaded = $true
      break
    }
  } catch {
    Write-Host "[install-electron] 失败: $_"
  }
}

if (-not $downloaded) {
  throw '镜像下载均失败，请检查网络或手动下载 zip 后重试'
}

Write-Host '[install-electron] 解压 ...'
if (Test-Path $distDir) { Remove-Item -Recurse -Force $distDir }
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
Expand-Archive -Path $zipPath -DestinationPath $distDir -Force
Remove-Item $zipPath -Force -ErrorAction SilentlyContinue

if (-not (Test-Path $exePath)) {
  throw "解压后未找到 $exePath"
}

Set-Content -Path (Join-Path $distDir 'path.txt') -Value $exePath -NoNewline
Write-Host "[install-electron] 完成: $exePath ($(('{0:N1}' -f ((Get-Item $exePath).Length / 1MB))) MB)"
