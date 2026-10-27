#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "o2_sensor.h"

#define O2_SENSOR_UART_BUF_SIZE 256
#define O2_SENSOR_READ_TIMEOUT_MS 600
#define O2_SENSOR_MODBUS_FUNC_READ_HOLDING_REGS 0x03
#define O2_SENSOR_MODBUS_REQUEST_SIZE 8
#define O2_SENSOR_MODBUS_RESPONSE_SIZE 7
#define O2_SENSOR_ACTIVE_FRAME_SIZE 9
#define O2_SENSOR_FAIL_LOG_INTERVAL 20

static const char *TAG = "o2_sensor";
static uint32_t s_read_fail_count;

static uint16_t be_u16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

#if O2_SENSOR_USE_MODBUS
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
#else
static uint8_t active_frame_checksum(const uint8_t *frame)
{
    uint8_t sum = 0;

    for (int i = 1; i < O2_SENSOR_ACTIVE_FRAME_SIZE - 1; i++)
    {
        sum += frame[i];
    }

    return (uint8_t)(0xFF - sum + 1);
}
#endif

void o2_sensor_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = O2_SENSOR_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(O2_SENSOR_UART_PORT,
                                        O2_SENSOR_UART_BUF_SIZE,
                                        0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(O2_SENSOR_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(O2_SENSOR_UART_PORT,
                                 O2_SENSOR_UART_TX_GPIO,
                                 O2_SENSOR_UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "init uart=%d tx=%d rx=%d baud=%d",
             O2_SENSOR_UART_PORT, O2_SENSOR_UART_TX_GPIO,
             O2_SENSOR_UART_RX_GPIO, O2_SENSOR_BAUD_RATE);
}

#if O2_SENSOR_USE_MODBUS
static bool read_o2(uint16_t *o2_x10)
{
    uint8_t request[O2_SENSOR_MODBUS_REQUEST_SIZE] = {
        O2_SENSOR_MODBUS_ADDR,
        O2_SENSOR_MODBUS_FUNC_READ_HOLDING_REGS,
        (uint8_t)(O2_SENSOR_MODBUS_REG >> 8),
        (uint8_t)(O2_SENSOR_MODBUS_REG & 0xFF),
        0x00,
        0x01,
        0x00,
        0x00,
    };
    uint16_t request_crc = modbus_crc16(request, O2_SENSOR_MODBUS_REQUEST_SIZE - 2);
    request[O2_SENSOR_MODBUS_REQUEST_SIZE - 2] = request_crc & 0xFF;
    request[O2_SENSOR_MODBUS_REQUEST_SIZE - 1] = request_crc >> 8;

    uart_flush_input(O2_SENSOR_UART_PORT);
    int written = uart_write_bytes(O2_SENSOR_UART_PORT, request, sizeof(request));
    if (written != sizeof(request))
    {
        return false;
    }
    uart_wait_tx_done(O2_SENSOR_UART_PORT, pdMS_TO_TICKS(100));

    uint8_t response[O2_SENSOR_MODBUS_RESPONSE_SIZE] = {0};
    int len = uart_read_bytes(O2_SENSOR_UART_PORT, response, sizeof(response),
                              pdMS_TO_TICKS(O2_SENSOR_READ_TIMEOUT_MS));
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

    if (response[0] != O2_SENSOR_MODBUS_ADDR ||
        response[1] != O2_SENSOR_MODBUS_FUNC_READ_HOLDING_REGS ||
        response[2] != 2)
    {
        return false;
    }

    uint16_t raw_value = be_u16(&response[3]);
#if O2_SENSOR_MODBUS_VALUE_SCALE_X10
    *o2_x10 = raw_value;
#else
    *o2_x10 = raw_value * 10;
#endif
    return true;
}
#else
static bool read_o2(uint16_t *o2_x10)
{
    uint8_t request[O2_SENSOR_ACTIVE_FRAME_SIZE] = {
        0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79,
    };
    uint8_t buf[O2_SENSOR_UART_BUF_SIZE] = {0};

    uart_flush_input(O2_SENSOR_UART_PORT);
    uart_write_bytes(O2_SENSOR_UART_PORT, request, sizeof(request));
    uart_wait_tx_done(O2_SENSOR_UART_PORT, pdMS_TO_TICKS(100));

    int len = uart_read_bytes(O2_SENSOR_UART_PORT, buf, sizeof(buf),
                              pdMS_TO_TICKS(O2_SENSOR_READ_TIMEOUT_MS));
    if (len < O2_SENSOR_ACTIVE_FRAME_SIZE)
    {
        return false;
    }

    for (int i = 0; i <= len - O2_SENSOR_ACTIVE_FRAME_SIZE; i++)
    {
        const uint8_t *frame = &buf[i];
        if (frame[0] != 0xFF || frame[1] != 0x86)
        {
            continue;
        }

        if (active_frame_checksum(frame) != frame[8])
        {
            continue;
        }

        *o2_x10 = be_u16(&frame[2]);
        return true;
    }

    return false;
}
#endif

bool o2_sensor_read(o2_sensor_data_t *data)
{
    if (data == NULL)
    {
        return false;
    }

    memset(data, 0, sizeof(*data));

    bool ok = read_o2(&data->o2_x10);

    if (ok)
    {
        s_read_fail_count = 0;
        return true;
    }

    s_read_fail_count++;
    if ((s_read_fail_count % O2_SENSOR_FAIL_LOG_INTERVAL) == 0)
    {
        ESP_LOGW(TAG, "read failed count=%lu", (unsigned long)s_read_fail_count);
    }

    return false;
}
