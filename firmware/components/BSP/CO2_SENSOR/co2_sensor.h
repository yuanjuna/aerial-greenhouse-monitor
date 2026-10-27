#ifndef CO2_SENSOR_H
#define CO2_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint16_t co2_ppm;
} co2_sensor_data_t;

void co2_sensor_init(void);
bool co2_sensor_read(co2_sensor_data_t *data);

#endif
