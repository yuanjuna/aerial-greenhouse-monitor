#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "app_config.h"
#include "mqtt_publisher.h"

static const char *TAG = "mqtt_publisher";

static QueueHandle_t s_sensor_queue;
static app_network_status_t *s_status;
static esp_mqtt_client_handle_t s_mqtt_client;
static uint32_t s_mqtt_publish_count;
static mqtt_control_callback_t s_control_callback;

static bool mqtt_topic_matches(const esp_mqtt_event_handle_t event, const char *topic)
{
    size_t topic_len = strlen(topic);

    return event->topic_len == (int)topic_len &&
           memcmp(event->topic, topic, topic_len) == 0;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    if (s_status == NULL)
    {
        return;
    }

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        s_status->mqtt_connected = true;
        ESP_LOGI(TAG, "mqtt connected");
        if (s_mqtt_client != NULL)
        {
            int msg_id = esp_mqtt_client_subscribe(s_mqtt_client,
                                                   MQTT_CONTROL_TOPIC, 0);
            ESP_LOGI(TAG, "mqtt subscribe topic=%s msg_id=%d",
                     MQTT_CONTROL_TOPIC, msg_id);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_status->mqtt_connected = false;
        ESP_LOGW(TAG, "mqtt disconnected");
        break;
    case MQTT_EVENT_ERROR:
        s_status->mqtt_connected = false;
        ESP_LOGE(TAG, "mqtt error");
        break;
    case MQTT_EVENT_DATA:
        if (event != NULL && s_control_callback != NULL &&
            mqtt_topic_matches(event, MQTT_CONTROL_TOPIC))
        {
            s_control_callback(event->data, event->data_len);
        }
        break;
    default:
        break;
    }
}

bool mqtt_publisher_start(const char *broker_uri)
{
    if (s_mqtt_client != NULL)
    {
        return true;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return false;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_mqtt_client,
                                                   ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler,
                                                   NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_mqtt_client_register_event failed: %s", esp_err_to_name(err));
        s_mqtt_client = NULL;
        return false;
    }

    err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
        s_mqtt_client = NULL;
        return false;
    }

    return true;
}

bool mqtt_publisher_is_started(void)
{
    return s_mqtt_client != NULL;
}

void mqtt_publisher_set_control_callback(mqtt_control_callback_t callback)
{
    s_control_callback = callback;
}

static void mqtt_publish_sensor(const sensor_sample_t *sample)
{
    if (s_status == NULL || !s_status->mqtt_connected || s_mqtt_client == NULL)
    {
        return;
    }

    char payload[384];
    int payload_len = snprintf(payload, sizeof(payload),
                               "{\"dht_ok\":%s,\"humidity\":%u,\"temperature\":%u,"
                               "\"light_percent\":%lu,"
                               "\"air_CO2\":%u,\"air_O2\":%.1f,"
                               "\"soil_ok\":%s,\"soil_moisture\":%.1f,"
                               "\"soil_temperature\":%.1f,\"soil_ec\":%u,"
                               "\"soil_ph\":%.1f,\"soil_n\":%u,"
                               "\"soil_p\":%u,\"soil_k\":%u}",
                               sample->dht_ok ? "true" : "false",
                               sample->dht_ok ? sample->humidity : 0,
                               sample->dht_ok ? sample->temperature : 0,
                               (unsigned long)sample->light_percent,
                               sample->air_co2_ok ? sample->air_co2_ppm : 0,
                               sample->air_o2_ok ? (sample->air_o2_x10 / 10.0f) : 0.0f,
                               sample->soil_ok ? "true" : "false",
                               sample->soil_ok ? (sample->soil_moisture_x10 / 10.0f) : 0.0f,
                               sample->soil_ok ? (sample->soil_temperature_x10 / 10.0f) : 0.0f,
                               sample->soil_ok ? sample->soil_ec_us_cm : 0,
                               sample->soil_ok ? (sample->soil_ph_x10 / 10.0f) : 0.0f,
                               sample->soil_ok ? sample->soil_n_mg_kg : 0,
                               sample->soil_ok ? sample->soil_p_mg_kg : 0,
                               sample->soil_ok ? sample->soil_k_mg_kg : 0);

    if (payload_len <= 0 || payload_len >= (int)sizeof(payload))
    {
        ESP_LOGW(TAG, "mqtt payload build failed");
        return;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC, payload,
                                         payload_len, 0, 0);
    if (msg_id < 0)
    {
        ESP_LOGW(TAG, "mqtt publish failed");
        return;
    }

    s_mqtt_publish_count++;
    if (s_mqtt_publish_count == 1 ||
        (s_mqtt_publish_count % MQTT_PUBLISH_LOG_INTERVAL) == 0)
    {
        ESP_LOGI(TAG, "mqtt publish count=%lu msg_id=%d topic=%s payload=%s",
                 (unsigned long)s_mqtt_publish_count, msg_id, MQTT_TOPIC, payload);
    }
}

static void mqtt_publish_task(void *arg)
{
    (void)arg;

    sensor_sample_t sample = {0};
    while (1)
    {
        if (s_sensor_queue != NULL &&
            xQueueReceive(s_sensor_queue, &sample, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            mqtt_publish_sensor(&sample);
        }
    }
}

void mqtt_publisher_init(QueueHandle_t sensor_queue, app_network_status_t *status)
{
    s_sensor_queue = sensor_queue;
    s_status = status;
    xTaskCreate(mqtt_publish_task, "mqtt_publish_task", 4096, NULL, 4, NULL);
}
