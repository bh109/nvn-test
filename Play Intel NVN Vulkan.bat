@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0diagnostics\Test-OpenSmashPort.ps1" -Live -Online
if errorlevel 1 pause
