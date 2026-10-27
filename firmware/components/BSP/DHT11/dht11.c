#include "dht11.h"
#include "esp_rom_sys.h"

static gpio_num_t s_dht11_gpio = GPIO_NUM_NC;

static void dht11_set_output(void)
{
    gpio_set_direction(s_dht11_gpio, GPIO_MODE_OUTPUT);
}

static void dht11_set_input(void)
{
    gpio_set_direction(s_dht11_gpio, GPIO_MODE_INPUT);
}

static int dht11_wait_level(uint32_t level, uint32_t timeout_us)
{
    while (timeout_us--)
    {
        if (gpio_get_level(s_dht11_gpio) == (int)level)
        {
            return 0;
        }
        esp_rom_delay_us(1);
    }

    return -1;
}

void dht11_init(gpio_num_t gpio_num)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    s_dht11_gpio = gpio_num;
    gpio_config(&io_conf);
    gpio_set_level(s_dht11_gpio, 1);
}

bool dht11_read(dht11_data_t *data)
{
    uint8_t bytes[5] = {0};

    if ((data == NULL) || (s_dht11_gpio == GPIO_NUM_NC))
    {
        return false;
    }

    dht11_set_output();
    gpio_set_level(s_dht11_gpio, 0);
    esp_rom_delay_us(20000);
    gpio_set_level(s_dht11_gpio, 1);
    esp_rom_delay_us(30);
    dht11_set_input();

    if (dht11_wait_level(0, 100) != 0)
    {
        return false;
    }
    if (dht11_wait_level(1, 100) != 0)
    {
        return false;
    }
    if (dht11_wait_level(0, 100) != 0)
    {
        return false;
    }

    for (int i = 0; i < 40; i++)
    {
        if (dht11_wait_level(1, 70) != 0)
        {
            return false;
        }

        esp_rom_delay_us(40);
        bytes[i / 8] <<= 1;
        if (gpio_get_level(s_dht11_gpio) == 1)
        {
            bytes[i / 8] |= 1;
        }

        if (dht11_wait_level(0, 100) != 0)
        {
            return false;
        }
    }

    if ((uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) != bytes[4])
    {
        return false;
    }

    data->humidity = bytes[0];
    data->temperature = bytes[2];
    return true;
}
