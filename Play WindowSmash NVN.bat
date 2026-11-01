@echo off
setlocal
cd /d "%~dp0"
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules;%PSModulePath%"
rem Resolution/performance is selected in Esc settings and saved in nvn_graphics.ini.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Run-NvnGame.ps1" -Live -Online
if errorlevel 1 pause
