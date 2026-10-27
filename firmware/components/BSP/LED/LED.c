#include "led.h"


/**
 * @brief       初始化LED
 * @param       无
 * @retval      无
 */
void led_init(void)
{
    gpio_config_t gpio_init_struct = {0};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /* 失能引脚中断 */
    gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;         /* 输入输出模式 */
    gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;      /* 失能上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /* 失能下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << LED0_GPIO_PIN;  /* 设置的引脚的位掩码 */
    ESP_ERROR_CHECK(gpio_config(&gpio_init_struct));        /* 配置GPIO */

    LED0(1);     /* 关闭LED0 */
}