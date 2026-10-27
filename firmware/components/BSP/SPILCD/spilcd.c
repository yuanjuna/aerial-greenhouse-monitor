#include "spilcd.h"
#include "spilcdfont.h"

DRAM_ATTR uint8_t refresh_done_flag = 0;    
esp_lcd_panel_handle_t panel_handle = NULL;
_spilcd_dev spilcddev;
#define SPI_LCD_TYPE    1           /* SPI接口屏幕类型（1：2.4寸SPILCD  0：1.3寸SPILCD） */ 

/* LCD的宽和高定义 */
#if SPI_LCD_TYPE                    /* 2.4寸SPI_LCD屏幕 */
uint16_t spilcd_width  = 320;       /* 屏幕的宽度 320(横屏) */
uint16_t spilcd_height = 240;       /* 屏幕的宽度 240(横屏) */
#else
uint16_t spilcd_width  = 240;       /* 屏幕的宽度 240(横屏) */
uint16_t spilcd_height = 240;       /* 屏幕的宽度 240(横屏) */
#endif                              /* 1.3寸SPI_LCD屏幕 */

static bool notify_lcd_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    refresh_done_flag = 1;
    return false;
}

static esp_err_t spilcd_draw_bitmap_wait(int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    refresh_done_flag = 0;

    esp_err_t err = esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, color_data);
    if (err != ESP_OK)
    {
        ESP_LOGE("spilcd", "draw bitmap failed: %s", esp_err_to_name(err));
        return err;
    }

    for (uint32_t retry = 0; retry < 200; retry++)
    {
        if (refresh_done_flag == 1)
        {
            return ESP_OK;
        }
        vTaskDelay(1);
    }

    ESP_LOGE("spilcd", "draw bitmap timeout");
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief       spilcd初始化
 * @param       无
 * @retval      ESP_OK:初始化成功
 */
esp_err_t spilcd_init(void)
{
    LCD_RST(0);
    vTaskDelay(pdMS_TO_TICKS(100));
    LCD_RST(1);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_panel_io_handle_t io_handle = NULL;     /* LCD IO设备句柄 */
    /* spi配置 */
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num         = LCD_DC_PIN,          /* DC IO */
        .cs_gpio_num         = LCD_CS_PIN,          /* CS IO */
        .pclk_hz             = 60 * 1000 * 1000,    /* PCLK为60MHz */
        .lcd_cmd_bits        = 8,                   /* 命令位宽 */
        .lcd_param_bits      = 8,                   /* LCD参数位宽 */
        .spi_mode            = 0,                   /* SPI模式 */
        .trans_queue_depth   = 7,                   /* 传输队列 */
    };
    /* 将LCD设备挂载至SPI总线上 */
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    spilcddev.pheight = spilcd_height;  /* 高度 */
    spilcddev.pwidth  = spilcd_width;   /* 宽度 */

    /* LCD设备配置 */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_PIN,                  /* RTS IO */
        .rgb_ele_order  = COLOR_RGB_ELEMENT_ORDER_RGB,  /* RGB颜色格式 */
        .bits_per_pixel = 16,                           /* 颜色深度 */
        .data_endian    = LCD_RGB_DATA_ENDIAN_BIG,      /* 大端顺序 */
    };
    /* 为ST7789创建LCD面板句柄，并指定SPI IO设备句柄 */
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    /* 复位LCD */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    /* 反显 */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    /* 初始化LCD句柄 */
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    /* 打开屏幕 */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lcd_flush_ready,
    };
    /* 注册屏幕刷新完成回调函数 */
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, NULL));

    spilcd_display_dir(1);      /* 横屏显示 */
    
    spilcd_clear(WHITE);        /* 清屏 */
    LCD_PWR(1);
    return ESP_OK;
}

/**
 * @brief       设置屏幕方向
 * @param       dir: 0为竖屏，1为横屏
 * @retval      无
 */
void spilcd_display_dir(uint8_t dir)
{
    spilcddev.dir = dir;

    if (spilcddev.dir == 0)         /* 竖屏 */
    {
        spilcddev.width = spilcddev.pheight;
        spilcddev.height = spilcddev.pwidth;
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, false);

        if (SPI_LCD_TYPE == 0)
        {
            esp_lcd_panel_set_gap(panel_handle, 0, 80);
        }
    }
    else if (spilcddev.dir == 1)    /* 横屏 */
    {
        spilcddev.width = spilcddev.pwidth;
        spilcddev.height = spilcddev.pheight;
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, false);

        if (SPI_LCD_TYPE == 0)
        {
            esp_lcd_panel_set_gap(panel_handle, 80, 0);
        }
    }
}

/**
 * @brief       清屏
 * @param       color: 颜色值
 * @retval      无
 */
void spilcd_clear(uint16_t color)
{
    /* 以40行作为缓冲,提高速率,若出现内存不足,可以减少缓冲行数 */
    uint16_t *buffer = heap_caps_malloc(spilcddev.width * sizeof(uint16_t) * 40, MALLOC_CAP_DMA);
    uint16_t color_tmp = ((color & 0x00FF) << 8) | ((color & 0xFF00) >> 8);   /* 需要转换一下颜色值 */
    if (NULL == buffer)
    {
        ESP_LOGE("TAG", "Memory for bitmap is not enough");
    }
    else
    {
        for (uint32_t i = 0; i < spilcddev.width * 40; i++)
        {
            buffer[i] = color_tmp;
        }
        
        for (uint16_t y = 0; y < spilcddev.height; y+=40)
        {
            spilcd_draw_bitmap_wait(0, y, spilcddev.width, y + 40, buffer);
        }
    }

    if (false)
    {
        /* 等待内部缓存刷新完成 */
        vTaskDelay(1);
    }
    

    heap_caps_free(buffer);
}

/**
 * @brief       在指定区域内填充单个颜色
 * @param       (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 * @param       color:要填充的颜色
 * @retval      无
 */
void spilcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
    /* 计算填充区域宽度 */
    uint16_t width = ex -sx;
    uint16_t height = ey - sy;

    /* 分配内存 */
    uint16_t *buffer = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    uint16_t color_tmp = ((color & 0x00FF) << 8) | ((color & 0xFF00) >> 8);   /* 需要转换一下颜色值 */

    if (NULL == buffer)
    {
        ESP_LOGE("TAG", "Memory for bitmap is not enough");
    }
    else
    {
        /* 填充颜色 */
        for (uint16_t i = 0; i < width; i++)
        {
            buffer[i] = color_tmp;
        }

        /* 绘制填充区域 */
        for (uint16_t y = 0; y < height; y++)
        {
            spilcd_draw_bitmap_wait(sx, sy + y, ex, sy + y + 1, buffer);
        }

        /* 释放内存 */
        heap_caps_free(buffer);
    }
}

/**
 * @brief       绘画一个像素点
 * @param       x    : x轴坐标
 * @param       y    : y轴坐标
 * @param       color: 颜色值
 * @retval      无
 */
void spilcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    uint16_t color_tmp = ((color & 0x00FF) << 8) | ((color & 0xFF00) >> 8);   /* 需要转换一下颜色值 */
    spilcd_draw_bitmap_wait(x, y, x + 1, y + 1, &color_tmp);
}

/**
 * @brief       画线函数(直线、斜线)
 * @param       x1,y1   起点坐标
 * @param       x2,y2   终点坐标
 * @param       color 填充颜色
 * @retval      无
 */
void spilcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, row, col;
    delta_x = x2 - x1;
    delta_y = y2 - y1;
    row = x1;
    col = y1;

    if (delta_x > 0)
    {
        incx = 1;
    }
    else if (delta_x == 0)
    {
        incx = 0;
    }
    else
    {
        incx = -1;
        delta_x = -delta_x;
    }

    if (delta_y > 0)
    {
        incy = 1;
    }
    else if (delta_y == 0)
    {
        incy = 0;
    }
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }

    if ( delta_x > delta_y)
    {
        distance = delta_x;
    }
    else
    {
        distance = delta_y;
    }

    for (t = 0; t <= distance + 1; t++)
    {
        spilcd_draw_point(row, col, color);
        xerr += delta_x;
        yerr += delta_y;

        if (xerr > distance)
        {
            xerr -= distance;
            row += incx;
        }

        if (yerr > distance)
        {
            yerr -= distance;
            col += incy;
        }
    }
}

/**
 * @brief       画水平线
 * @param       x0,y0: 起点坐标
 * @param       len  : 线长度
 * @param       color: 矩形的颜色
 * @retval      无
 */
void spilcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    /* 确保坐标在LCD范围内 */
    if (len == 0 || x >= spilcddev.width || y >= spilcddev.height) return;

    uint16_t ex = fmin(spilcddev.width - 1, x + len - 1);
    uint16_t ey = y;

    /* 填充颜色区域 */
    uint32_t width = ex - x + 1;
    uint32_t h = ey - y + 1;
    uint16_t *color_buffer = malloc(width * h * sizeof(uint16_t));

    if (color_buffer == NULL) 
    {
        return; /* 检查内存分配是否成功 */
    }

    for (uint32_t i = 0; i < width * h; i++)
    {
        color_buffer[i] = color;
    }

    spilcd_draw_bitmap_wait(x, y, ex + 1, ey + 1, color_buffer);
    free(color_buffer);
}

/**
 * @brief       画一个矩形
 * @param       x1,y1   起点坐标
 * @param       x2,y2   终点坐标
 * @param       color 填充颜色
 * @retval      无
 */
void spilcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color)
{
    spilcd_draw_line(x0, y0, x1, y0,color);
    spilcd_draw_line(x0, y0, x0, y1,color);
    spilcd_draw_line(x0, y1, x1, y1,color);
    spilcd_draw_line(x1, y0, x1, y1,color);
}

/**
 * @brief       画一个圆
 * @param       x0,y0   圆心坐标
 * @param       r   圆半径
 * @param       color 填充颜色
 * @retval      无
 */
void spilcd_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int a, b;
    int di;
    a = 0;
    b = r;
    di = 3 - (r << 1);

    while (a <= b)
    {
        spilcd_draw_point(x0 - b, y0 - a, color);
        spilcd_draw_point(x0 + b, y0 - a, color);
        spilcd_draw_point(x0 - a, y0 + b, color);
        spilcd_draw_point(x0 - b, y0 - a, color);
        spilcd_draw_point(x0 - a, y0 - b, color);
        spilcd_draw_point(x0 + b, y0 + a, color);
        spilcd_draw_point(x0 + a, y0 - b, color);
        spilcd_draw_point(x0 + a, y0 + b, color);
        spilcd_draw_point(x0 - b, y0 + a, color);
        a++;

        if (di < 0)
        {
            di += 4 * a + 6;
        }
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }

        spilcd_draw_point(x0 + a, y0 + b, color);
    }
}

/**
 * @brief       在指定位置显示一个字符
 * @param       x,y  : 坐标
 * @param       chr  : 要显示的字符:" "--->"~"
 * @param       size : 字体大小 12/16/24/32
 * @param       mode : 叠加方式(1); 非叠加方式(0);
 * @param       color : 字符的颜色;
 * @retval      无
 */
void spilcd_show_char(uint16_t x, uint16_t y, uint8_t chr, uint8_t size, uint8_t mode, uint16_t color)
{
    const uint8_t *ch_code;     /* 存放chr字符对应数组的首地址 */
    uint8_t ch_width;           /* 字符的宽度 */
    uint8_t ch_height;          /* 字符的高度 */
    uint8_t ch_size;            /* 字符的大小(字节) */
    uint8_t ch_offset;          /* 字符在字库的相对位置 */
    uint8_t byte_index;         /* 字符对应数据的索引值 */
    uint8_t byte_code;          /* 字符对应数据 */
    uint8_t bit_index;          /* 字符对应字节数据的位索引 */
    uint16_t colortemp = 0;     /* 颜色数据 */
    uint16_t pix_index = 0;
    uint16_t *pcolor = NULL;

    /* 字体大小(字节) =       字体宽度占用字体大小              * 字体高度 */
    ch_size = ((size / 2) / 8 +  (((size / 2) % 8) ? 1 : 0)) * size;         /* 得到字体一个字符对应点阵集所占的字节数 */

    pcolor = heap_caps_malloc(size * (size / 2) * 2, MALLOC_CAP_DMA);        /* 申请大小 */
    uint16_t color_tmp = ((color & 0x00FF) << 8) | ((color & 0xFF00) >> 8);  /* 需要转换一下颜色值 */

    if (NULL == pcolor)
    {
        ESP_LOGE("TAG", "Memory for bitmap is not enough");
    }

    ch_offset = chr - ' ';    /* 得到偏移后的值（ASCII字库是从空格开始取模，所以-' '就是对应字符的字库） */

    switch (size)
    {
        case 12:
            ch_code = (uint8_t *)asc2_1206[ch_offset];  /* 调用1206字体 */
            ch_width = 6;
            ch_height = 12;
            break;

        case 16:
            ch_code = (uint8_t *)asc2_1608[ch_offset];  /* 调用1608字体 */
            ch_width = 8;
            ch_height = 16;
            break;

        case 24:
            ch_code = (uint8_t *)asc2_2412[ch_offset];  /* 调用2412字体 */
            ch_width = 12;
            ch_height = 24;
            break;

        case 32:
            ch_code = (uint8_t *)asc2_3216[ch_offset];  /* 调用3216字体 */
            ch_width = 16;
            ch_height = 32;
            break;

        default:
            return ;
    }

    for (byte_index = 0; byte_index < ch_size; byte_index++)
    {
        byte_code = ch_code[byte_index];    /* 获取字符的点阵数据 */

        for (bit_index = 0; bit_index < 8; bit_index++)   /* 一个字节8个点 */
        {
            if ((byte_code & 0x80) != 0)        /* 有效点,需要显示 */
            {
                colortemp = color_tmp;  
            }
            else if (mode == 0)     /* 无效点,不显示 */
            {
                colortemp = 0xFFFF;
            }

            pcolor[pix_index] = colortemp;
            pix_index++;

            if ((size == 24) && (byte_index % 2))   /* 24号字体比较特殊,奇数字节只有四位有效 */
            {
                if (bit_index == 3)
                {
                    break;
                }
            }

            byte_code <<= 1; /* 移位, 以便获取下一个位的状态 */
        }
    }

    spilcd_draw_bitmap_wait(x, y, x + ch_width, y + ch_height, (uint16_t *)pcolor);

    refresh_done_flag = 0;

    if (false)
    {
        /* 等待内部缓存刷新完成 */
        vTaskDelay(1);
    }
    

    heap_caps_free(pcolor);
}

/**
 * @brief       m^n函数
 * @param       m,n: 输入参数
 * @retval      m^n次方
 */
uint32_t lcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while(n--) result *= m;

    return result;
}

/**
 * @brief       显示len个数字
 * @param       x,y : 起始坐标
 * @param       num : 数值(0 ~ 2^32)
 * @param       len : 显示数字的位数
 * @param       size: 选择字体 12/16/24/32
 * @retval      无
 */
void spilcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                               /* 按总显示位数循环 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                       /* 获取对应位的数字 */

        if (enshow == 0 && t < (len - 1))                                   /* 没有使能显示,且还有位要显示 */
        {
            if (temp == 0)
            {
                spilcd_show_char(x + (size / 2)*t, y, ' ', size, 0, color);    /* 显示空格,占位 */
                continue;                                                   /* 继续下个一位 */
            }
            else
            {
                enshow = 1;                                                 /* 使能显示 */
            }

        }

        spilcd_show_char(x + (size / 2)*t, y, temp + '0', size, 0, color);     /* 显示字符 */
    }
}

/**
 * @brief       扩展显示len个数字(高位是0也显示)
 * @param       x,y : 起始坐标
 * @param       num : 数值(0 ~ 2^32)
 * @param       len : 显示数字的位数
 * @param       size: 选择字体 12/16/24/32
 * @param       mode: 显示模式
 *              [7]:0,不填充;1,填充0.
 *              [6:1]:保留
 *              [0]:0,非叠加显示;1,叠加显示.
 * @param       color : 数字的颜色;
 * @retval      无
 */
void spilcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                                           /* 按总显示位数循环 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                                   /* 获取对应位的数字 */

        if (enshow == 0 && t < (len - 1))                                               /* 没有使能显示,且还有位要显示 */
        {
            if (temp == 0)
            {
                if (mode & 0X80)                                                        /* 高位需要填充0 */
                {
                    spilcd_show_char(x + (size / 2)*t, y, '0', size, mode & 0X01, color);  /* 用0占位 */
                }
                else
                {
                    spilcd_show_char(x + (size / 2)*t, y, ' ', size, mode & 0X01, color);  /* 用空格占位 */
                }
                continue;
            }
            else
            {
                enshow = 1;                                                             /* 使能显示 */
            }
        }
        spilcd_show_char(x + (size / 2)*t, y, temp + '0', size, mode & 0X01, color);
    }
}

/**
 * @brief       显示字符串
 * @param       x,y         : 起始坐标
 * @param       width,height: 区域大小
 * @param       size        : 选择字体 12/16/24/32
 * @param       p           : 字符串首地址
 * @retval      无
 */
void spilcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
{
    uint8_t x0 = x;
    width += x;
    height += y;

    while ((*p <= '~') && (*p >= ' '))   /* 判断是不是非法字符! */
    {
        if (x >= width)
        {
            x = x0;
            y += size;
        }

        if (y >= height) break;  /* 退出 */

        spilcd_show_char(x, y, *p, size, 0, color);

        x += size / 2;
        p++;
    }
}
