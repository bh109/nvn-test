@echo off
setlocal
cd /d "%~dp0"
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules;%PSModulePath%"
rem NVN setup uses the existing game-file wizard without upstream self-updating.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install_windowsmash.ps1" -Nvn -NoUpdate %*
set "nvn_install_exit=%errorlevel%"
echo.
pause
exit /b %nvn_install_exit%
