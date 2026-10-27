#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "adc.h"
#include "dht11.h"
#include "myiic.h"
#include "my_spi.h"
#include "xl9555.h"
#include "spilcd.h"
#include "soil_sensor.h"
#include "co2_sensor.h"
#include "o2_sensor.h"
#include "app_types.h"
#include "mqtt_publisher.h"
#include "network_manager.h"
#include "sensor_lcd_app.h"

static const char *TAG = "sensor_lcd_app";

#define SUPPLEMENT_DARK_ON_THRESHOLD    45
#define SUPPLEMENT_BRIGHT_OFF_THRESHOLD 30
#define SUPPLEMENT_LED_GPIO_PIN         GPIO_NUM_14
#define SUPPLEMENT_LED_ON_LEVEL         1
#define SUPPLEMENT_LED_OFF_LEVEL        0
#define WATER_PUMP_DRY_ON_THRESHOLD_X10 300
#define WATER_PUMP_WET_OFF_THRESHOLD_X10 450
#define WATER_PUMP_MIN_SWITCH_INTERVAL_MS 3000
#define WATER_PUMP_GPIO_PIN             GPIO_NUM_15
#define WATER_PUMP_ON_LEVEL             1
#define WATER_PUMP_OFF_LEVEL            0
#define DHT11_READ_RETRY_COUNT          3
#define DHT11_RETRY_DELAY_MS            60
#define DHT11_MAX_CONSECUTIVE_FAILS     3
#define HUMIDIFIER_DRY_ON_THRESHOLD     59
#define HUMIDIFIER_WET_OFF_THRESHOLD    60
#define HUMIDIFIER_MIN_SWITCH_INTERVAL_MS 3000
#define HUMIDIFIER_GPIO_PIN             GPIO_NUM_10
#define HUMIDIFIER_ON_LEVEL             1
#define HUMIDIFIER_OFF_LEVEL            0

typedef enum
{
    ACTUATOR_MODE_AUTO = 0,
    ACTUATOR_MODE_FORCE_ON,
    ACTUATOR_MODE_FORCE_OFF,
} actuator_mode_t;

static app_network_status_t s_network_status;
static QueueHandle_t s_sensor_queue;
static bool s_supplement_light_on;
static bool s_water_pump_on;
static TickType_t s_water_pump_last_switch_tick;
static volatile actuator_mode_t s_water_pump_mode = ACTUATOR_MODE_FORCE_OFF;
static bool s_humidifier_on;
static TickType_t s_humidifier_last_switch_tick;
static volatile actuator_mode_t s_humidifier_mode = ACTUATOR_MODE_FORCE_OFF;
static dht11_data_t s_last_dht_data;
static bool s_has_last_dht_data;
static uint8_t s_dht_fail_count;

static void lcd_show_text(uint16_t x, uint16_t y, uint16_t width,
                          uint8_t size, const char *text, uint16_t color)
{
    spilcd_show_string(x, y, width, size, size, (char *)text, color);
}

static void lcd_show_centered(uint16_t y, uint8_t size,
                              const char *text, uint16_t color)
{
    uint16_t text_width = 0;
    const char *p = text;

    while (*p != '\0')
    {
        text_width += size / 2;
        p++;
    }

    uint16_t x = 0;
    if (text_width < spilcddev.width)
    {
        x = (spilcddev.width - text_width) / 2;
    }

    lcd_show_text(x, y, spilcddev.width - x, size, text, color);
}

static void lcd_show_line(uint16_t x, uint16_t y, uint16_t width,
                          const char *text, uint16_t color)
{
    char line[32];

    snprintf(line, sizeof(line), "%-31s", text);
    lcd_show_text(x, y, width, 16, line, color);
}

static void format_tenth(char *buf, size_t buf_size, int32_t value_x10,
                         const char *unit)
{
    int32_t abs_value = value_x10;
    const char *sign = "";

    if (value_x10 < 0)
    {
        sign = "-";
        abs_value = -value_x10;
    }

    snprintf(buf, buf_size, "%s%ld.%ld%s", sign,
             (long)(abs_value / 10), (long)(abs_value % 10), unit);
}

static void draw_static_ui(void)
{
    spilcd_clear(WHITE);
    lcd_show_centered(8, 24, "GREENHOUSE LIVE DATA", RED);
    spilcd_draw_hline(10, 38, 300, LGRAYBLUE);

    lcd_show_text(16, 48, 140, 16, "AIR SENSOR", GRAYBLUE);
    lcd_show_text(174, 48, 140, 16, "RS485 SOIL", GRAYBLUE);
    spilcd_draw_hline(16, 66, 128, LGRAY);
    spilcd_draw_hline(174, 66, 128, LGRAY);

    lcd_show_line(16, 76, 148, "air_humi : --%", BLUE);
    lcd_show_line(16, 98, 148, "air_temp : --C", BLUE);
    lcd_show_line(16, 120, 148, "air_light: --%", BLUE);
    lcd_show_line(16, 142, 148, "air_CO2  : ----", BLUE);
    lcd_show_line(16, 164, 148, "air_O2   : --.-%", BLUE);

    lcd_show_line(174, 72, 140, "moist: --.-%", BLUE);
    lcd_show_line(174, 90, 140, "temp : --.-C", BLUE);
    lcd_show_line(174, 108, 140, "ec   : ----", BLUE);
    lcd_show_line(174, 126, 140, "ph   : --.-", BLUE);
    lcd_show_line(174, 144, 140, "n    : ----", BLUE);
    lcd_show_line(174, 162, 140, "p    : ----", BLUE);
    lcd_show_line(174, 180, 140, "k    : ----", BLUE);

    spilcd_draw_hline(10, 204, 300, LGRAYBLUE);
    lcd_show_text(12, 216, 96, 16, "SENSOR:--", GRAYBLUE);
    lcd_show_text(120, 216, 80, 16, "SOIL:--", GRAYBLUE);
    lcd_show_text(216, 216, 96, 16, "MQTT:--", GRAYBLUE);
}

static void update_network_status(void)
{
    if (s_network_status.mqtt_connected)
    {
        lcd_show_text(216, 216, 96, 16, "MQTT:OK  ", GREEN);
    }
    else if (s_network_status.wifi_connected)
    {
        lcd_show_text(216, 216, 96, 16, "MQTT:FAIL", RED);
    }
    else
    {
        lcd_show_text(216, 216, 96, 16, "MQTT:WAIT", GRAYBLUE);
    }
}

static void update_supplement_light(uint32_t light_percent)
{
    if (!s_supplement_light_on && light_percent >= SUPPLEMENT_DARK_ON_THRESHOLD)
    {
        s_supplement_light_on = true;
        gpio_set_level(SUPPLEMENT_LED_GPIO_PIN, SUPPLEMENT_LED_ON_LEVEL);
    }
    else if (s_supplement_light_on && light_percent <= SUPPLEMENT_BRIGHT_OFF_THRESHOLD)
    {
        s_supplement_light_on = false;
        gpio_set_level(SUPPLEMENT_LED_GPIO_PIN, SUPPLEMENT_LED_OFF_LEVEL);
    }
}

static void supplement_led_init(void)
{
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
    gpio_init_struct.mode = GPIO_MODE_OUTPUT;
    gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pin_bit_mask = 1ull << SUPPLEMENT_LED_GPIO_PIN;
    ESP_ERROR_CHECK(gpio_config(&gpio_init_struct));

    gpio_set_level(SUPPLEMENT_LED_GPIO_PIN, SUPPLEMENT_LED_OFF_LEVEL);
    s_supplement_light_on = false;
}

static void set_water_pump_output(bool on)
{
    s_water_pump_on = on;
    s_water_pump_last_switch_tick = xTaskGetTickCount();
    gpio_set_level(WATER_PUMP_GPIO_PIN,
                   on ? WATER_PUMP_ON_LEVEL : WATER_PUMP_OFF_LEVEL);
}

static void set_humidifier_output(bool on)
{
    s_humidifier_on = on;
    s_humidifier_last_switch_tick = xTaskGetTickCount();
    gpio_set_level(HUMIDIFIER_GPIO_PIN,
                   on ? HUMIDIFIER_ON_LEVEL : HUMIDIFIER_OFF_LEVEL);
}

static void update_water_pump(uint16_t soil_moisture_x10)
{
    if (s_water_pump_mode != ACTUATOR_MODE_AUTO)
    {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    TickType_t min_interval = pdMS_TO_TICKS(WATER_PUMP_MIN_SWITCH_INTERVAL_MS);

    if (!s_water_pump_on && soil_moisture_x10 <= WATER_PUMP_DRY_ON_THRESHOLD_X10)
    {
        if ((s_water_pump_last_switch_tick != 0) &&
            ((now - s_water_pump_last_switch_tick) < min_interval))
        {
            return;
        }

        s_water_pump_on = true;
        s_water_pump_last_switch_tick = now;
        gpio_set_level(WATER_PUMP_GPIO_PIN, WATER_PUMP_ON_LEVEL);
    }
    else if (s_water_pump_on && soil_moisture_x10 >= WATER_PUMP_WET_OFF_THRESHOLD_X10)
    {
        if ((s_water_pump_last_switch_tick != 0) &&
            ((now - s_water_pump_last_switch_tick) < min_interval))
        {
            return;
        }

        s_water_pump_on = false;
        s_water_pump_last_switch_tick = now;
        gpio_set_level(WATER_PUMP_GPIO_PIN, WATER_PUMP_OFF_LEVEL);
    }
}

static void water_pump_init(void)
{
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
    gpio_init_struct.mode = GPIO_MODE_OUTPUT;
    gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pin_bit_mask = 1ull << WATER_PUMP_GPIO_PIN;
    ESP_ERROR_CHECK(gpio_config(&gpio_init_struct));

    gpio_set_level(WATER_PUMP_GPIO_PIN, WATER_PUMP_OFF_LEVEL);
    s_water_pump_on = false;
    s_water_pump_last_switch_tick = 0;
    s_water_pump_mode = ACTUATOR_MODE_FORCE_OFF;
}

static void update_humidifier(uint8_t humidity)
{
    if (s_humidifier_mode != ACTUATOR_MODE_AUTO)
    {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    TickType_t min_interval = pdMS_TO_TICKS(HUMIDIFIER_MIN_SWITCH_INTERVAL_MS);

    if (!s_humidifier_on && humidity <= HUMIDIFIER_DRY_ON_THRESHOLD)
    {
        if ((s_humidifier_last_switch_tick != 0) &&
            ((now - s_humidifier_last_switch_tick) < min_interval))
        {
            return;
        }

        s_humidifier_on = true;
        s_humidifier_last_switch_tick = now;
        gpio_set_level(HUMIDIFIER_GPIO_PIN, HUMIDIFIER_ON_LEVEL);
    }
    else if (s_humidifier_on && humidity >= HUMIDIFIER_WET_OFF_THRESHOLD)
    {
        if ((s_humidifier_last_switch_tick != 0) &&
            ((now - s_humidifier_last_switch_tick) < min_interval))
        {
            return;
        }

        s_humidifier_on = false;
        s_humidifier_last_switch_tick = now;
        gpio_set_level(HUMIDIFIER_GPIO_PIN, HUMIDIFIER_OFF_LEVEL);
    }
}

static void humidifier_init(void)
{
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
    gpio_init_struct.mode = GPIO_MODE_OUTPUT;
    gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pin_bit_mask = 1ull << HUMIDIFIER_GPIO_PIN;
    ESP_ERROR_CHECK(gpio_config(&gpio_init_struct));

    gpio_set_level(HUMIDIFIER_GPIO_PIN, HUMIDIFIER_OFF_LEVEL);
    s_humidifier_on = false;
    s_humidifier_last_switch_tick = 0;
    s_humidifier_mode = ACTUATOR_MODE_FORCE_OFF;
}

static const char *actuator_mode_name(actuator_mode_t mode)
{
    switch (mode)
    {
    case ACTUATOR_MODE_FORCE_ON:
        return "on";
    case ACTUATOR_MODE_FORCE_OFF:
        return "off";
    case ACTUATOR_MODE_AUTO:
    default:
        return "auto";
    }
}

static void set_water_pump_mode(actuator_mode_t mode)
{
    s_water_pump_mode = mode;

    if (mode == ACTUATOR_MODE_FORCE_ON)
    {
        set_water_pump_output(true);
    }
    else if (mode == ACTUATOR_MODE_FORCE_OFF)
    {
        set_water_pump_output(false);
    }

    ESP_LOGI(TAG, "mqtt control water pump mode=%s",
             actuator_mode_name(mode));
}

static void set_humidifier_mode(actuator_mode_t mode)
{
    s_humidifier_mode = mode;

    if (mode == ACTUATOR_MODE_FORCE_ON)
    {
        set_humidifier_output(true);
    }
    else if (mode == ACTUATOR_MODE_FORCE_OFF)
    {
        set_humidifier_output(false);
    }

    ESP_LOGI(TAG, "mqtt control humidifier mode=%s",
             actuator_mode_name(mode));
}

static bool payload_has(const char *payload, const char *needle)
{
    return strstr(payload, needle) != NULL;
}

static bool payload_has_state(const char *payload, const char *state)
{
    char quoted_state[12];

    snprintf(quoted_state, sizeof(quoted_state), "\"%s\"", state);
    return payload_has(payload, quoted_state);
}

static void handle_mqtt_control(const char *payload, int payload_len)
{
    if (payload == NULL || payload_len <= 0)
    {
        return;
    }

    char command[160];
    size_t copy_len = payload_len < (int)(sizeof(command) - 1) ?
                      (size_t)payload_len : sizeof(command) - 1;

    memcpy(command, payload, copy_len);
    command[copy_len] = '\0';

    bool want_on = payload_has_state(command, "on");
    bool want_off = payload_has_state(command, "off");
    bool want_auto = payload_has_state(command, "auto");

    if (payload_has(command, "pump"))
    {
        if (want_on)
        {
            set_water_pump_mode(ACTUATOR_MODE_FORCE_ON);
        }
        else if (want_off)
        {
            set_water_pump_mode(ACTUATOR_MODE_FORCE_OFF);
        }
        else if (want_auto)
        {
            set_water_pump_mode(ACTUATOR_MODE_AUTO);
        }
    }

    if (payload_has(command, "humidifier") || payload_has(command, "atom"))
    {
        if (want_on)
        {
            set_humidifier_mode(ACTUATOR_MODE_FORCE_ON);
        }
        else if (want_off)
        {
            set_humidifier_mode(ACTUATOR_MODE_FORCE_OFF);
        }
        else if (want_auto)
        {
            set_humidifier_mode(ACTUATOR_MODE_AUTO);
        }
    }
}

static bool read_dht11_with_retry(dht11_data_t *data)
{
    for (int retry = 0; retry < DHT11_READ_RETRY_COUNT; retry++)
    {
        if (dht11_read(data))
        {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(DHT11_RETRY_DELAY_MS));
    }

    return false;
}

static void init_board(void)
{
    supplement_led_init();
    water_pump_init();
    humidifier_init();
    adc_init();
    my_spi_init();
    myiic_init();
    xl9555_init();
    spilcd_init();
    dht11_init(DHT11_GPIO_PIN);
    co2_sensor_init();
    soil_sensor_init();
    o2_sensor_init();
}

void sensor_lcd_app_start(void)
{
    dht11_data_t dht_data = {0};
    co2_sensor_data_t co2_data = {0};
    o2_sensor_data_t o2_data = {0};
    soil_sensor_data_t soil_data = {0};
    sensor_sample_t sample = {0};
    char line[32];
    char value[16];

    init_board();
    draw_static_ui();

    s_sensor_queue = xQueueCreate(1, sizeof(sensor_sample_t));
    if (s_sensor_queue == NULL)
    {
        ESP_LOGE(TAG, "failed to create sensor queue");
    }

    mqtt_publisher_init(s_sensor_queue, &s_network_status);
    mqtt_publisher_set_control_callback(handle_mqtt_control);
    network_manager_start(&s_network_status);

    while (1)
    {
        uint32_t adc_value = adc_get_result_average(ADC_CHAN, 10);
        sample.light_percent = adc_value * 100 / 4095;
        if (sample.light_percent > 100)
        {
            sample.light_percent = 100;
        }
        update_supplement_light(sample.light_percent);

        bool dht_read_ok = read_dht11_with_retry(&dht_data);
        if (dht_read_ok)
        {
            s_last_dht_data = dht_data;
            s_has_last_dht_data = true;
            s_dht_fail_count = 0;
            sample.dht_ok = true;
            sample.humidity = dht_data.humidity;
            sample.temperature = dht_data.temperature;
            update_humidifier(sample.humidity);
            lcd_show_text(12, 216, 96, 16, "SENSOR:OK ", GREEN);
        }
        else if (s_has_last_dht_data &&
                 s_dht_fail_count < DHT11_MAX_CONSECUTIVE_FAILS)
        {
            s_dht_fail_count++;
            sample.dht_ok = true;
            sample.humidity = s_last_dht_data.humidity;
            sample.temperature = s_last_dht_data.temperature;
            update_humidifier(sample.humidity);
            lcd_show_text(12, 216, 96, 16, "SENSOR:HOLD", GRAYBLUE);
            ESP_LOGW(TAG, "dht11 read failed, use last value count=%u",
                     s_dht_fail_count);
        }
        else
        {
            s_dht_fail_count++;
            sample.dht_ok = false;
            sample.humidity = 0;
            sample.temperature = 0;
            if (s_humidifier_mode == ACTUATOR_MODE_AUTO)
            {
                set_humidifier_output(false);
            }
            lcd_show_text(12, 216, 96, 16, "SENSOR:ERR", RED);
            ESP_LOGW(TAG, "dht11 read failed count=%u", s_dht_fail_count);
        }

        snprintf(line, sizeof(line), "air_humi : %3u%%", sample.humidity);
        lcd_show_line(16, 76, 148, line, sample.dht_ok ? BLUE : GRAYBLUE);
        snprintf(line, sizeof(line), "air_temp : %3uC", sample.temperature);
        lcd_show_line(16, 98, 148, line, sample.dht_ok ? BLUE : GRAYBLUE);
        snprintf(line, sizeof(line), "air_light: %3lu%%",
                 (unsigned long)sample.light_percent);
        lcd_show_line(16, 120, 148, line, BLUE);

        sample.air_co2_ok = co2_sensor_read(&co2_data);
        if (sample.air_co2_ok)
        {
            sample.air_co2_ppm = co2_data.co2_ppm;
        }
        else
        {
            sample.air_co2_ppm = 0;
        }

        snprintf(line, sizeof(line), "air_CO2  : %4uppm", sample.air_co2_ppm);
        lcd_show_line(16, 142, 148, line, sample.air_co2_ok ? BLUE : GRAYBLUE);

        sample.soil_ok = soil_sensor_read(&soil_data);
        if (sample.soil_ok)
        {
            sample.soil_moisture_x10 = soil_data.moisture_x10;
            sample.soil_temperature_x10 = soil_data.temperature_x10;
            sample.soil_ec_us_cm = soil_data.ec_us_cm;
            sample.soil_ph_x10 = soil_data.ph_x10;
            sample.soil_n_mg_kg = soil_data.n_mg_kg;
            sample.soil_p_mg_kg = soil_data.p_mg_kg;
            sample.soil_k_mg_kg = soil_data.k_mg_kg;
            update_water_pump(sample.soil_moisture_x10);
            lcd_show_text(120, 216, 80, 16, "SOIL:OK ", GREEN);
        }
        else
        {
            sample.soil_moisture_x10 = 0;
            sample.soil_temperature_x10 = 0;
            sample.soil_ec_us_cm = 0;
            sample.soil_ph_x10 = 0;
            sample.soil_n_mg_kg = 0;
            sample.soil_p_mg_kg = 0;
            sample.soil_k_mg_kg = 0;
            lcd_show_text(120, 216, 80, 16, "SOIL:ERR", RED);
        }

        format_tenth(value, sizeof(value), sample.soil_moisture_x10, "%");
        snprintf(line, sizeof(line), "moist: %s", value);
        lcd_show_line(174, 72, 140, line, sample.soil_ok ? BLUE : GRAYBLUE);

        format_tenth(value, sizeof(value), sample.soil_temperature_x10, "C");
        snprintf(line, sizeof(line), "temp : %s", value);
        lcd_show_line(174, 90, 140, line, sample.soil_ok ? BLUE : GRAYBLUE);

        snprintf(line, sizeof(line), "ec   : %4u", sample.soil_ec_us_cm);
        lcd_show_line(174, 108, 140, line, sample.soil_ok ? BLUE : GRAYBLUE);

        format_tenth(value, sizeof(value), sample.soil_ph_x10, "");
        snprintf(line, sizeof(line), "ph   : %s", value);
        lcd_show_line(174, 126, 140, line, sample.soil_ok ? BLUE : GRAYBLUE);

        snprintf(line, sizeof(line), "n    : %4u", sample.soil_n_mg_kg);
        lcd_show_line(174, 144, 140, line, sample.soil_ok ? BLUE : GRAYBLUE);

        snprintf(line, sizeof(line), "p    : %4u", sample.soil_p_mg_kg);
        lcd_show_line(174, 162, 140, line, sample.soil_ok ? BLUE : GRAYBLUE);

        snprintf(line, sizeof(line), "k    : %4u", sample.soil_k_mg_kg);
        lcd_show_line(174, 180, 140, line, sample.soil_ok ? BLUE : GRAYBLUE);

        sample.air_o2_ok = o2_sensor_read(&o2_data);
        if (sample.air_o2_ok)
        {
            sample.air_o2_x10 = o2_data.o2_x10;
        }
        else
        {
            sample.air_o2_x10 = 0;
        }

        format_tenth(value, sizeof(value), sample.air_o2_x10, "%");
        snprintf(line, sizeof(line), "air_O2   : %s", value);
        lcd_show_line(16, 164, 148, line, sample.air_o2_ok ? BLUE : GRAYBLUE);

        update_network_status();

        ESP_LOGI(TAG, "dht_ok=%d humi=%u%% temp=%uC humidifier=%d light=%lu%% "
                      "air_co2_ok=%d air_co2=%u air_o2_ok=%d air_o2=%.1f%% "
                      "soil_ok=%d soil_moisture=%.1f%% soil_temp=%.1fC ec=%u ph=%.1f n=%u p=%u k=%u",
                 sample.dht_ok, sample.humidity, sample.temperature,
                 s_humidifier_on,
                 (unsigned long)sample.light_percent,
                 sample.air_co2_ok, sample.air_co2_ppm,
                 sample.air_o2_ok, sample.air_o2_x10 / 10.0f,
                 sample.soil_ok,
                 sample.soil_moisture_x10 / 10.0f,
                 sample.soil_temperature_x10 / 10.0f,
                 sample.soil_ec_us_cm,
                 sample.soil_ph_x10 / 10.0f,
                 sample.soil_n_mg_kg,
                 sample.soil_p_mg_kg,
                 sample.soil_k_mg_kg);

        if (s_sensor_queue != NULL)
        {
            xQueueOverwrite(s_sensor_queue, &sample);
        }

        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
