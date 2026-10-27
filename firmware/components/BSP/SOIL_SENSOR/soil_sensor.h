#ifndef SOIL_SENSOR_H
#define SOIL_SENSOR_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/uart.h"

#ifndef SOIL_SENSOR_UART_PORT
#define SOIL_SENSOR_UART_PORT UART_NUM_1
#endif

#ifndef SOIL_SENSOR_UART_TX_GPIO
#define SOIL_SENSOR_UART_TX_GPIO GPIO_NUM_18
#endif

#ifndef SOIL_SENSOR_UART_RX_GPIO
#define SOIL_SENSOR_UART_RX_GPIO GPIO_NUM_17
#endif

#ifndef SOIL_SENSOR_UART_RTS_GPIO
#define SOIL_SENSOR_UART_RTS_GPIO GPIO_NUM_NC
#endif

#ifndef SOIL_SENSOR_MODBUS_ADDR
#define SOIL_SENSOR_MODBUS_ADDR 1
#endif

#ifndef SOIL_SENSOR_BAUD_RATE
#define SOIL_SENSOR_BAUD_RATE 9600
#endif

typedef struct
{
    uint16_t moisture_x10;
    int16_t temperature_x10;
    uint16_t ec_us_cm;
    uint16_t ph_x10;
    uint16_t n_mg_kg;
    uint16_t p_mg_kg;
    uint16_t k_mg_kg;
} soil_sensor_data_t;

void soil_sensor_init(void);
bool soil_sensor_read(soil_sensor_data_t *data);

#endif
