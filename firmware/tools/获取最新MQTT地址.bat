@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Get-MQTT-Address.ps1"
pause
