@echo off
setlocal
cd /d "%~dp0.."
echo [build-all.bat] 顺序编译 mxt-app + serial-app（不含打包）
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-all.ps1" %*
if errorlevel 1 exit /b 1
