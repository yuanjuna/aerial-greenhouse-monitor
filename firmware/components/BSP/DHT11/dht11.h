#ifndef __DHT11_H
#define __DHT11_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

#define DHT11_GPIO_PIN GPIO_NUM_3

typedef struct
{
    uint8_t temperature;
    uint8_t humidity;
} dht11_data_t;

void dht11_init(gpio_num_t gpio_num);
bool dht11_read(dht11_data_t *data);

#endif
