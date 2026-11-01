@echo off
setlocal
cd /d "%~dp0"
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules;%PSModulePath%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\uninstall_nvn.ps1" %*
set "nvn_uninstall_exit=%errorlevel%"
echo.
pause
exit /b %nvn_uninstall_exit%
