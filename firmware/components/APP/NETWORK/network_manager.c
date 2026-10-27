#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "app_config.h"
#include "mqtt_publisher.h"
#include "network_manager.h"

static const char *TAG = "network_manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num;
static esp_netif_t *s_sta_netif;
static app_network_status_t *s_status;

static bool check_esp_ok(esp_err_t err, const char *what)
{
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
        return false;
    }

    return true;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;

        if (s_retry_num < WIFI_MAX_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP, reason=%d", event->reason);
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }

        if (s_status != NULL)
        {
            s_status->wifi_connected = false;
            s_status->mqtt_connected = false;
        }
        ESP_LOGW(TAG, "connect to the AP failed, reason=%d", event->reason);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        if (s_status != NULL)
        {
            s_status->wifi_connected = true;
        }
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "failed to create Wi-Fi event group");
        return false;
    }

    if (!check_esp_ok(esp_netif_init(), "esp_netif_init"))
    {
        return false;
    }

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return false;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL)
    {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
        return false;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (!check_esp_ok(esp_wifi_init(&cfg), "esp_wifi_init") ||
        !check_esp_ok(esp_wifi_set_storage(WIFI_STORAGE_RAM), "esp_wifi_set_storage"))
    {
        return false;
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    if (!check_esp_ok(esp_event_handler_instance_register(WIFI_EVENT,
                                                          ESP_EVENT_ANY_ID,
                                                          &wifi_event_handler,
                                                          NULL,
                                                          &instance_any_id),
                      "register Wi-Fi event handler") ||
        !check_esp_ok(esp_event_handler_instance_register(IP_EVENT,
                                                          IP_EVENT_STA_GOT_IP,
                                                          &wifi_event_handler,
                                                          NULL,
                                                          &instance_got_ip),
                      "register IP event handler"))
    {
        return false;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    if (!check_esp_ok(esp_wifi_set_mode(WIFI_MODE_STA), "esp_wifi_set_mode") ||
        !check_esp_ok(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), "esp_wifi_set_config") ||
        !check_esp_ok(esp_wifi_start(), "esp_wifi_start"))
    {
        return false;
    }

    ESP_LOGI(TAG, "wifi_init_sta finished");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s", WIFI_SSID);
        return true;
    }

    ESP_LOGE(TAG, "failed to connect to SSID:%s", WIFI_SSID);
    return false;
}

static void build_mqtt_uri(char *uri, size_t uri_size, const char *host)
{
    snprintf(uri, uri_size, "mqtt://%s:%d", host, MQTT_BROKER_PORT);
}

static bool tcp_port_is_open(const char *host, uint16_t port, int timeout_ms)
{
    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &dest_addr.sin_addr) != 1)
    {
        ESP_LOGW(TAG, "invalid broker ip: %s", host);
        return false;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0)
    {
        ESP_LOGW(TAG, "socket create failed: errno=%d", errno);
        return false;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
    {
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err == 0)
    {
        close(sock);
        return true;
    }

    if (errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EAGAIN)
    {
        close(sock);
        return false;
    }

    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(sock, &write_set);

    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };

    err = select(sock + 1, NULL, &write_set, NULL, &timeout);
    if (err <= 0)
    {
        close(sock);
        return false;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0)
    {
        close(sock);
        return false;
    }

    close(sock);
    return so_error == 0;
}

static bool try_broker_host(char *uri, size_t uri_size, const char *host,
                            int timeout_ms, bool log_probe)
{
    if (log_probe)
    {
        ESP_LOGI(TAG, "probing MQTT broker %s:%d", host, MQTT_BROKER_PORT);
    }

    if (!tcp_port_is_open(host, MQTT_BROKER_PORT, timeout_ms))
    {
        return false;
    }

    build_mqtt_uri(uri, uri_size, host);
    ESP_LOGI(TAG, "MQTT broker reachable: %s", uri);
    return true;
}

static bool discover_mqtt_broker(char *uri, size_t uri_size)
{
    if (try_broker_host(uri, uri_size, MQTT_KNOWN_BROKER_HOST, MQTT_BROKER_PROBE_MS, true) ||
        try_broker_host(uri, uri_size, MQTT_DEFAULT_BROKER_HOST, MQTT_BROKER_PROBE_MS, true))
    {
        return true;
    }

    if (s_sta_netif == NULL)
    {
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_sta_netif, &ip_info) != ESP_OK)
    {
        return false;
    }

    char gateway[16];
    snprintf(gateway, sizeof(gateway), IPSTR, IP2STR(&ip_info.gw));
    if (try_broker_host(uri, uri_size, gateway, MQTT_BROKER_PROBE_MS, true))
    {
        return true;
    }

    uint32_t ip = ntohl(ip_info.ip.addr);
    uint32_t mask = ntohl(ip_info.netmask.addr);
    uint32_t network = ip & mask;
    uint32_t broadcast = network | (~mask);

    if ((broadcast - network) > 255)
    {
        network = ip & 0xFFFFFF00;
        broadcast = network | 0x000000FF;
    }

    ESP_LOGI(TAG, "scanning local subnet for MQTT broker on port %d", MQTT_BROKER_PORT);
    for (uint32_t candidate = network + 1; candidate < broadcast; candidate++)
    {
        if (candidate == ip || candidate == ntohl(ip_info.gw.addr))
        {
            continue;
        }

        esp_ip4_addr_t addr = {
            .addr = htonl(candidate),
        };
        char host[16];
        snprintf(host, sizeof(host), IPSTR, IP2STR(&addr));

        if (try_broker_host(uri, uri_size, host, MQTT_SUBNET_PROBE_MS, false))
        {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGW(TAG, "no reachable MQTT broker found");
    return false;
}

static void network_task(void *arg)
{
    (void)arg;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        if (check_esp_ok(nvs_flash_erase(), "nvs_flash_erase"))
        {
            ret = nvs_flash_init();
        }
    }

    if (ret == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition not found, Wi-Fi will use RAM storage");
    }
    else if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "nvs_flash_init failed: %s, Wi-Fi will try RAM storage", esp_err_to_name(ret));
    }

    if (wifi_init_sta())
    {
        while (!mqtt_publisher_is_started())
        {
            char broker_uri[MQTT_URI_SIZE];
            if (discover_mqtt_broker(broker_uri, sizeof(broker_uri)))
            {
                mqtt_publisher_start(broker_uri);
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(MQTT_BROKER_RETRY_MS));
        }
    }

    vTaskDelete(NULL);
}

void network_manager_start(app_network_status_t *status)
{
    s_status = status;
    xTaskCreate(network_task, "network_task", 8192, NULL, 5, NULL);
}
