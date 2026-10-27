#include "my_spi.h"


/* SD卡设备句柄 */
spi_device_handle_t MY_SD_Handle = NULL;

/**
 * @brief       spi初始化
 * @param       无
 * @retval      esp_err_t
 */
esp_err_t my_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .sclk_io_num     = SPI_SCLK_PIN,    /* 时钟引脚 */
        .mosi_io_num     = SPI_MOSI_PIN,    /* 主机输出从机输入引脚 */
        .miso_io_num     = SPI_MISO_PIN,    /* 主机输入从机输出引脚 */
        .quadwp_io_num   = -1,              /* 用于Quad模式的WP引脚,未使用时设置为-1 */
        .quadhd_io_num   = -1,              /* 用于Quad模式的HD引脚,未使用时设置为-1 */
        .max_transfer_sz = 320 * 240 * sizeof(uint16_t),   /* 最大传输大小(整屏(RGB565格式)) */
    };
    /* 初始化SPI总线 */
    ESP_ERROR_CHECK(spi_bus_initialize(MY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* SPI驱动接口配置,SPISD卡时钟是20-25MHz */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000, /* SPI时钟 */
        .mode = 0,                          /* SPI模式0 */
        .spics_io_num = SD_CS_PIN,          /* 片选引脚 */
        .queue_size = 7,                    /* 事务队列尺寸 7个 */
    };

    /* 添加SPI总线设备 */
    ESP_ERROR_CHECK(spi_bus_add_device(MY_SPI_HOST, &devcfg, &MY_SD_Handle));

    return ESP_OK;
}