#ifndef __SPILCD_H
#define __SPILCD_H

#include "driver/gpio.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_log.h"
#include "esp_lcd_panel_st7789.h"
#include "xl9555.h"
#include "math.h"

#define LCD_RST_PIN         GPIO_NUM_NC
#define LCD_PWR_PIN         GPIO_NUM_NC
#define LCD_DC_PIN          GPIO_NUM_40
#define LCD_CS_PIN          GPIO_NUM_21

#define LCD_PWR(x)          do { x ?                                \
                                 xl9555_pin_write(SLCD_PWR_IO, 1):  \
                                 xl9555_pin_write(SLCD_PWR_IO, 0);  \
                            } while(0)

#define LCD_RST(x)          do { x ?                                 \
                                 xl9555_pin_write(SLCD_RST_IO, 1):   \
                                 xl9555_pin_write(SLCD_RST_IO, 0);   \
                            } while(0)

#define LCD_HOST            SPI2_HOST

#define WHITE               0xFFFF
#define BLACK               0x0000
#define RED                 0xF800
#define GREEN               0x07E0
#define BLUE                0x001F
#define MAGENTA             0XF81F
#define YELLOW              0XFFE0
#define CYAN                0X07FF
#define BROWN               0XBC40
#define BRRED               0XFC07
#define GRAY                0X8430
#define DARKBLUE            0X01CF
#define LIGHTBLUE           0X7D7C
#define GRAYBLUE            0X5458
#define LIGHTGREEN          0X841F
#define LGRAY               0XC618
#define LGRAYBLUE           0XA651
#define LBBLUE              0X2B12

typedef struct
{
    uint32_t pwidth;
    uint32_t pheight;
    uint8_t  dir;
    uint16_t width;
    uint16_t height;
} _spilcd_dev;

extern _spilcd_dev spilcddev;
extern esp_lcd_panel_handle_t panel_handle;

esp_err_t spilcd_init(void);
void spilcd_display_dir(uint8_t dir);
void spilcd_clear(uint16_t color);
void spilcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color);
void spilcd_draw_point(uint16_t x, uint16_t y, uint16_t color);
void spilcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void spilcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);
void spilcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void spilcd_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void spilcd_show_char(uint16_t x, uint16_t y, uint8_t chr, uint8_t size, uint8_t mode, uint16_t color);
void spilcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color);
void spilcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color);
void spilcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color);

#endif
