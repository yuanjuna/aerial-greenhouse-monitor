$Port = 1883
$OutFile = Join-Path $PSScriptRoot "Latest-MQTT-Address.txt"

# MQTT 账号可用环境变量覆盖：$env:MQTT_USER / $env:MQTT_PASS
$MqttUser = if ($env:MQTT_USER) { $env:MQTT_USER } else { "esp32" }
$MqttPass = if ($env:MQTT_PASS) { $env:MQTT_PASS } else { "YOUR_MQTT_PASSWORD" }

$route = Get-NetRoute -DestinationPrefix "0.0.0.0/0" -ErrorAction SilentlyContinue |
  Sort-Object RouteMetric, InterfaceMetric |
  Select-Object -First 1

if ($route) {
  $ip = (Get-NetIPAddress -AddressFamily IPv4 -InterfaceIndex $route.InterfaceIndex -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike "169.254.*" } |
    Select-Object -First 1 -ExpandProperty IPAddress)
}

if (-not $ip) {
  $ip = (Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object {
      $_.IPAddress -notlike "127.*" -and
      $_.IPAddress -notlike "169.254.*" -and
      $_.PrefixOrigin -ne "WellKnown"
    } |
    Select-Object -First 1 -ExpandProperty IPAddress)
}

if (-not $ip) {
  Write-Host "No valid local IPv4 address found."
  Write-Host "Please connect this PC to WiFi / phone hotspot first."
  exit 1
}

$mqttUrl = "mqtt://$ip`:$Port"

try {
  $ok = (Test-NetConnection $ip -Port $Port -WarningAction SilentlyContinue).TcpTestSucceeded
} catch {
  $ok = $false
}

if ($ok) {
  $status = "MQTT port 1883: OPEN"
} else {
  $status = "MQTT port 1883: CLOSED or Mosquitto is not running"
}

$text = @"
Current MQTT address:
$mqttUrl

Unreal Server Url:
$mqttUrl

ESP32 / Raspberry Pi Broker IP:
$ip

User Name:
$MqttUser

Password:
$MqttPass

Sensor Topic:
greenhouse/esp32s3_01/sensor

Control Topic:
greenhouse/esp32s3_01/control

$status

Generated:
$(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
"@

$text | Set-Content -Path $OutFile -Encoding UTF8
$mqttUrl | Set-Clipboard

Write-Host "========================================"
Write-Host "Current MQTT address:"
Write-Host $mqttUrl
Write-Host ""
Write-Host "Copied to clipboard."
Write-Host "Saved to:"
Write-Host $OutFile
Write-Host ""
Write-Host $status
Write-Host "========================================"
