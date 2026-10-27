#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/**
 * 通用配置模板（可提交到仓库）。
 *
 * 真实的 Wi-Fi / MQTT 账号请写在同一目录下的 app_config_local.h，
 * 该文件已被 .gitignore 忽略，不会被提交。示例见仓库 README。
 *
 * 没有 app_config_local.h 时，下面每个宏都会退回到占位默认值。
 */

#if defined(__has_include)
#  if __has_include("app_config_local.h")
#    include "app_config_local.h"
#  endif
#endif

/* ---------- 可覆盖项 ---------- */

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#endif

/* 上次成功连接过的 Broker，开机优先尝试 */
#ifndef MQTT_KNOWN_BROKER_HOST
#define MQTT_KNOWN_BROKER_HOST "192.168.1.100"
#endif

/* 兜底 Broker，局域网探测失败时使用 */
#ifndef MQTT_DEFAULT_BROKER_HOST
#define MQTT_DEFAULT_BROKER_HOST "192.168.1.100"
#endif

#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT 1883
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME "esp32"
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD "YOUR_MQTT_PASSWORD"
#endif

/* 传感器数据上报主题 */
#ifndef MQTT_TOPIC
#define MQTT_TOPIC "greenhouse/esp32s3_01/sensor"
#endif

/* 远程控制指令主题 */
#ifndef MQTT_CONTROL_TOPIC
#define MQTT_CONTROL_TOPIC "greenhouse/esp32s3_01/control"
#endif

/* ---------- 固定参数 ---------- */

#define MQTT_URI_SIZE 64

/* 每推送 N 次数据打印一条日志 */
#define MQTT_PUBLISH_LOG_INTERVAL 20

/* 与 Broker 断开后的重连间隔 */
#define MQTT_BROKER_RETRY_MS 10000

/* 已连接 Broker 的存活探测周期 */
#define MQTT_BROKER_PROBE_MS 1000

/* 局域网扫描探测 Broker 的步进 */
#define MQTT_SUBNET_PROBE_MS 180

#define WIFI_MAX_RETRY 5

#endif
