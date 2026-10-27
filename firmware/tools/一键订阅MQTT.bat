@echo off
title MQTT Subscriber

:: MQTT 账号与 Mosquitto 安装目录，可通过环境变量覆盖
if not defined MQTT_USER set "MQTT_USER=esp32"
if not defined MQTT_PASS set "MQTT_PASS=YOUR_MQTT_PASSWORD"
if not defined MOSQUITTO_DIR set "MOSQUITTO_DIR=C:\Program Files\mosquitto"

:: Request admin permission
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo Stopping Mosquitto service...
net stop mosquitto >nul 2>&1

echo.
echo Starting Mosquitto broker manually...
pushd "%MOSQUITTO_DIR%"
start "Mosquitto Broker" cmd /k mosquitto -c mosquitto.conf -v
popd

echo.
echo Waiting for broker...
timeout /t 3 /nobreak >nul

echo.
echo Checking port 1883...
netstat -ano | findstr :1883

echo.
echo Starting MQTT subscriber...
echo Broker: 127.0.0.1:1883
echo User: %MQTT_USER%
echo Topic: #
echo.

cd /d "%MOSQUITTO_DIR%"
mosquitto_sub -h 127.0.0.1 -p 1883 -u %MQTT_USER% -P %MQTT_PASS% -t "#" -v

pause
