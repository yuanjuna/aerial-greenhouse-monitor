#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soil_sensor.h"

#define SOIL_SENSOR_UART_BUF_SIZE 256
#define SOIL_SENSOR_READ_TIMEOUT_MS 800
#define SOIL_SENSOR_FRAME_GAP_MS 20
#define MODBUS_FUNC_READ_HOLDING_REGS 0x03
#define SOIL_SENSOR_REG_START 0x0000
#define SOIL_SENSOR_REG_COUNT 7
#define SOIL_SENSOR_REQUEST_SIZE 8
#define SOIL_SENSOR_RESPONSE_SIZE (5 + (SOIL_SENSOR_REG_COUNT * 2))
#define SOIL_SENSOR_FAIL_LOG_INTERVAL 20

static const char *TAG = "soil_sensor";
static uint32_t s_read_fail_count;

static uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            if ((crc & 0x0001) != 0)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static uint16_t be_u16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

void soil_sensor_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = SOIL_SENSOR_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(SOIL_SENSOR_UART_PORT,
                                        SOIL_SENSOR_UART_BUF_SIZE,
                                        0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(SOIL_SENSOR_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(SOIL_SENSOR_UART_PORT,
                                 SOIL_SENSOR_UART_TX_GPIO,
                                 SOIL_SENSOR_UART_RX_GPIO,
                                 SOIL_SENSOR_UART_RTS_GPIO,
                                 UART_PIN_NO_CHANGE));

    if (SOIL_SENSOR_UART_RTS_GPIO != GPIO_NUM_NC)
    {
        ESP_ERROR_CHECK(uart_set_mode(SOIL_SENSOR_UART_PORT, UART_MODE_RS485_HALF_DUPLEX));
    }

    ESP_LOGI(TAG, "init ok addr=%d baud=%d tx=%d rx=%d rts=%d",
             SOIL_SENSOR_MODBUS_ADDR, SOIL_SENSOR_BAUD_RATE,
             SOIL_SENSOR_UART_TX_GPIO, SOIL_SENSOR_UART_RX_GPIO,
             SOIL_SENSOR_UART_RTS_GPIO);
}

static bool soil_sensor_read_with_func(uint8_t func, soil_sensor_data_t *data)
{
    uint8_t request[SOIL_SENSOR_REQUEST_SIZE] = {
        SOIL_SENSOR_MODBUS_ADDR,
        func,
        (uint8_t)(SOIL_SENSOR_REG_START >> 8),
        (uint8_t)(SOIL_SENSOR_REG_START & 0xFF),
        0x00,
        SOIL_SENSOR_REG_COUNT,
        0x00,
        0x00,
    };
    uint16_t request_crc = modbus_crc16(request, SOIL_SENSOR_REQUEST_SIZE - 2);
    request[SOIL_SENSOR_REQUEST_SIZE - 2] = request_crc & 0xFF;
    request[SOIL_SENSOR_REQUEST_SIZE - 1] = request_crc >> 8;

    uart_flush_input(SOIL_SENSOR_UART_PORT);
    int written = uart_write_bytes(SOIL_SENSOR_UART_PORT, request, sizeof(request));
    if (written != sizeof(request))
    {
        ESP_LOGW(TAG, "request write failed written=%d", written);
        return false;
    }
    uart_wait_tx_done(SOIL_SENSOR_UART_PORT, pdMS_TO_TICKS(100));

    uint8_t response[SOIL_SENSOR_RESPONSE_SIZE] = {0};
    int len = uart_read_bytes(SOIL_SENSOR_UART_PORT, response, sizeof(response),
                              pdMS_TO_TICKS(SOIL_SENSOR_READ_TIMEOUT_MS));
    vTaskDelay(pdMS_TO_TICKS(SOIL_SENSOR_FRAME_GAP_MS));

    if (len != sizeof(response))
    {
        return false;
    }

    uint16_t response_crc = modbus_crc16(response, len - 2);
    uint16_t received_crc = ((uint16_t)response[len - 1] << 8) | response[len - 2];
    if (response_crc != received_crc)
    {
        return false;
    }

    if (response[0] != SOIL_SENSOR_MODBUS_ADDR ||
        response[1] != func ||
        response[2] != (SOIL_SENSOR_REG_COUNT * 2))
    {
        return false;
    }

    const uint8_t *regs = &response[3];
    data->moisture_x10 = be_u16(&regs[0]);
    data->temperature_x10 = (int16_t)be_u16(&regs[2]);
    data->ec_us_cm = be_u16(&regs[4]);
    data->ph_x10 = be_u16(&regs[6]);
    data->n_mg_kg = be_u16(&regs[8]);
    data->p_mg_kg = be_u16(&regs[10]);
    data->k_mg_kg = be_u16(&regs[12]);

    return true;
}

bool soil_sensor_read(soil_sensor_data_t *data)
{
    if (data == NULL)
    {
        return false;
    }

    if (soil_sensor_read_with_func(MODBUS_FUNC_READ_HOLDING_REGS, data))
    {
        s_read_fail_count = 0;
        return true;
    }

    s_read_fail_count++;
    if ((s_read_fail_count % SOIL_SENSOR_FAIL_LOG_INTERVAL) == 0)
    {
        ESP_LOGW(TAG, "read failed count=%lu", (unsigned long)s_read_fail_count);
    }

    return false;
}
