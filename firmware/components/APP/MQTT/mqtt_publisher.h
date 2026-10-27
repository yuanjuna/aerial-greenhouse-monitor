#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include <stdbool.h>
#include "freertos/queue.h"
#include "app_types.h"

typedef void (*mqtt_control_callback_t)(const char *payload, int payload_len);

void mqtt_publisher_init(QueueHandle_t sensor_queue, app_network_status_t *status);
bool mqtt_publisher_start(const char *broker_uri);
bool mqtt_publisher_is_started(void);
void mqtt_publisher_set_control_callback(mqtt_control_callback_t callback);

#endif
