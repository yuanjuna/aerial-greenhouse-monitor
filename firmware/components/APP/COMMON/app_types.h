#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool dht_ok;
    uint8_t humidity;
    uint8_t temperature;
    uint32_t light_percent;
    bool air_co2_ok;
    uint16_t air_co2_ppm;
    bool air_o2_ok;
    uint16_t air_o2_x10;
    bool soil_ok;
    uint16_t soil_moisture_x10;
    int16_t soil_temperature_x10;
    uint16_t soil_ec_us_cm;
    uint16_t soil_ph_x10;
    uint16_t soil_n_mg_kg;
    uint16_t soil_p_mg_kg;
    uint16_t soil_k_mg_kg;
} sensor_sample_t;

typedef struct
{
    volatile bool wifi_connected;
    volatile bool mqtt_connected;
} app_network_status_t;

#endif
