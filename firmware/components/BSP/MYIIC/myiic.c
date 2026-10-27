#include "myiic.h"


i2c_master_bus_handle_t bus_handle;     /* 总线句柄 */

/**
 * @brief       初始化MYIIC
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t myiic_init(void)
{
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source                     = I2C_CLK_SRC_DEFAULT,  /* 时钟源 */
        .i2c_port                       = IIC_NUM_PORT,         /* I2C端口 */
        .scl_io_num                     = IIC_SCL_GPIO_PIN,     /* SCL管脚 */
        .sda_io_num                     = IIC_SDA_GPIO_PIN,     /* SDA管脚 */
        .glitch_ignore_cnt              = 7,                    /* 故障周期 */
        .flags.enable_internal_pullup   = true,                 /* 内部上拉 */
    };
    /* 新建I2C总线 */
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    return ESP_OK;
}
