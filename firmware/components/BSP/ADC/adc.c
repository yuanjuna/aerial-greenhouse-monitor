#include "adc.h"

adc_oneshot_unit_handle_t adc_handle = NULL;

void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t adc_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHAN, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, CO2_ADC_CHAN, &config));
}

#define LOST_VAL 1

uint32_t adc_get_result_average(adc_channel_t ch, uint32_t times)
{
    uint32_t sum = 0;
    int temp_val = 0;

    if ((adc_handle == NULL) || (times <= 2 * LOST_VAL))
    {
        ESP_LOGE("adc", "invalid adc average request");
        return 0;
    }

    int rawdata[times];

    for (uint32_t t = 0; t < times; t++)
    {
        esp_err_t err = adc_oneshot_read(adc_handle, ch, &rawdata[t]);
        if (err != ESP_OK)
        {
            ESP_LOGE("adc", "adc read failed: %s", esp_err_to_name(err));
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    for (uint32_t i = 0; i < times - 1; i++)
    {
        for (uint32_t j = i + 1; j < times; j++)
        {
            if (rawdata[i] > rawdata[j])
            {
                temp_val = rawdata[i];
                rawdata[i] = rawdata[j];
                rawdata[j] = temp_val;
            }
        }
    }

    for (uint32_t i = LOST_VAL; i < times - LOST_VAL; i++)
    {
        sum += rawdata[i];
    }

    return sum / (times - 2 * LOST_VAL);
}
