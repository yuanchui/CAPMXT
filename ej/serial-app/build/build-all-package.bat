@echo off
setlocal
cd /d "%~dp0.."
echo [build-all-package.bat] 顺序编译并打 NSIS 安装包
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-all.ps1" -Package %*
if errorlevel 1 exit /b 1
