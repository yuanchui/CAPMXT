# Windows 一键：生成加密文件并上传到服务器
# 用法: powershell -File build/deploy-runtime-window/deploy.ps1
$ErrorActionPreference = 'Stop'
$Root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location $Root

if (-not (Test-Path 'build/deploy-runtime-window/runtime-deploy.env')) {
    Copy-Item 'build/deploy-runtime-window/runtime-deploy.env.example' 'build/deploy-runtime-window/runtime-deploy.env'
    Write-Host '已创建 runtime-deploy.env，请编辑 REMOTE_USER 后重新运行' -ForegroundColor Yellow
    exit 1
}

node build/generate-runtime-window.js
node build/deploy-runtime-window/upload.js
