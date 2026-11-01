@echo off
setlocal
cd /d "%~dp0"
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules;%PSModulePath%"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Start-NvnLobby.ps1" %*
set "lobby_exit=%errorlevel%"
echo.
pause
exit /b %lobby_exit%
