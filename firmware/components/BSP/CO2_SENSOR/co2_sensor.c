#include "adc.h"
#include "co2_sensor.h"

#define CO2_SENSOR_SAMPLE_COUNT 10

#ifndef CO2_SENSOR_CLEAN_AIR_RAW
#define CO2_SENSOR_CLEAN_AIR_RAW 400
#endif

#ifndef CO2_SENSOR_CLEAN_AIR_PPM
#define CO2_SENSOR_CLEAN_AIR_PPM 400
#endif

#ifndef CO2_SENSOR_HIGH_RAW
#define CO2_SENSOR_HIGH_RAW 3000
#endif

#ifndef CO2_SENSOR_HIGH_PPM
#define CO2_SENSOR_HIGH_PPM 5000
#endif

void co2_sensor_init(void)
{
}

static uint16_t co2_raw_to_ppm(uint32_t raw)
{
    if (raw <= CO2_SENSOR_CLEAN_AIR_RAW)
    {
        return CO2_SENSOR_CLEAN_AIR_PPM;
    }

    if (raw >= CO2_SENSOR_HIGH_RAW)
    {
        return CO2_SENSOR_HIGH_PPM;
    }

    uint32_t raw_span = CO2_SENSOR_HIGH_RAW - CO2_SENSOR_CLEAN_AIR_RAW;
    uint32_t ppm_span = CO2_SENSOR_HIGH_PPM - CO2_SENSOR_CLEAN_AIR_PPM;
    uint32_t ppm = CO2_SENSOR_CLEAN_AIR_PPM +
                   ((raw - CO2_SENSOR_CLEAN_AIR_RAW) * ppm_span) / raw_span;

    return (uint16_t)ppm;
}

bool co2_sensor_read(co2_sensor_data_t *data)
{
    if (data == NULL)
    {
        return false;
    }

    uint32_t raw = adc_get_result_average(CO2_ADC_CHAN, CO2_SENSOR_SAMPLE_COUNT);
    data->co2_ppm = co2_raw_to_ppm(raw);
    return true;
}
