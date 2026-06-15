@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "MSYS2=%MSYS2_ROOT%"
if not defined MSYS2 set "MSYS2=C:\msys64"

if not exist "%MSYS2%\msys2_shell.cmd" (
  echo [build-win64-msys2] 未找到 MSYS2: %MSYS2%
  exit /b 1
)

"%MSYS2%\msys2_shell.cmd" -mingw64 -defterm -no-start -c "cd \"$(cygpath -u '%SCRIPT_DIR%')\" && bash ./build-win64-msys2.sh"
exit /b %ERRORLEVEL%
