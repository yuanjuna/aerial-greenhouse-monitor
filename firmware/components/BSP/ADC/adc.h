#ifndef __ADC_H
#define __ADC_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

/* Light sensor AO uses GPIO4 on ESP32-S3 */
#define ADC_CHAN    ADC_CHANNEL_3
/* CO2 sensor AO uses GPIO5 on ESP32-S3 */
#define CO2_ADC_CHAN ADC_CHANNEL_4

void adc_init(void);
uint32_t adc_get_result_average(adc_channel_t ch, uint32_t times);

#endif
