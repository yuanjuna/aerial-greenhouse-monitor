#ifndef O2_SENSOR_H
#define O2_SENSOR_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/uart.h"

#ifndef O2_SENSOR_UART_PORT
#define O2_SENSOR_UART_PORT UART_NUM_2
#endif

#ifndef O2_SENSOR_UART_TX_GPIO
#define O2_SENSOR_UART_TX_GPIO GPIO_NUM_16
#endif

#ifndef O2_SENSOR_UART_RX_GPIO
#define O2_SENSOR_UART_RX_GPIO GPIO_NUM_7
#endif

#ifndef O2_SENSOR_BAUD_RATE
#define O2_SENSOR_BAUD_RATE 9600
#endif

#ifndef O2_SENSOR_USE_MODBUS
#define O2_SENSOR_USE_MODBUS 0
#endif

#ifndef O2_SENSOR_MODBUS_ADDR
#define O2_SENSOR_MODBUS_ADDR 2
#endif

#ifndef O2_SENSOR_MODBUS_REG
#define O2_SENSOR_MODBUS_REG 0x0000
#endif

#ifndef O2_SENSOR_MODBUS_VALUE_SCALE_X10
#define O2_SENSOR_MODBUS_VALUE_SCALE_X10 1
#endif

typedef struct
{
    uint16_t o2_x10;
} o2_sensor_data_t;

void o2_sensor_init(void);
bool o2_sensor_read(o2_sensor_data_t *data);

#endif
