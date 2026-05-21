#include "lcd.h"
#include "hardware.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "lcd";
static lv_display_t *s_display = NULL;

static const struct {
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
} s_rotation = {
    .swap_xy  = true,
    .mirror_x = true,
    .mirror_y = false,
};

esp_err_t lcd_init(void)
{
    /* LVGL port init */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    /* LEDC backlight */
    ledc_timer_config_t ledc_timer = {
        .speed_mode    = LEDC_MODE,
        .timer_num     = LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz       = LEDC_FREQ_HZ,
        .clk_cfg       = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode    = LEDC_MODE,
        .channel       = LEDC_CHANNEL,
        .timer_sel     = LEDC_TIMER,
        .intr_type     = LEDC_INTR_DISABLE,
        .gpio_num      = PIN_LCD_BL,
        .duty          = 128,
        .hpoint        = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    /* SPI bus */
    spi_bus_config_t buscfg = {
        .sclk_io_num     = PIN_LCD_SCLK,
        .mosi_io_num     = PIN_LCD_MOSI,
        .miso_io_num     = PIN_LCD_MISO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_DRAWBUF_SIZE * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* Panel IO */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num     = PIN_LCD_DC,
        .cs_gpio_num     = PIN_LCD_CS,
        .pclk_hz         = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits    = LCD_CMD_BITS,
        .lcd_param_bits  = LCD_PARAM_BITS,
        .spi_mode        = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &io_handle));

    /* ST7789 panel */
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, s_rotation.swap_xy));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, s_rotation.mirror_x, s_rotation.mirror_y));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* LVGL display */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle      = io_handle,
        .panel_handle   = panel_handle,
        .buffer_size    = LCD_DRAWBUF_SIZE,
        .double_buffer  = LCD_DOUBLE_BUFFER,
        .hres           = LCD_H_RES,
        .vres           = LCD_V_RES,
        .monochrome     = false,
        .color_format   = LV_COLOR_FORMAT_RGB565,
        .rounder_cb     = NULL,
        .rotation       = {
            .swap_xy    = s_rotation.swap_xy,
            .mirror_x   = s_rotation.mirror_x,
            .mirror_y   = s_rotation.mirror_y,
        },
        .flags = {
            .buff_dma   = true,
            .swap_bytes = true,
        },
    };

    s_display = lvgl_port_add_disp(&disp_cfg);
    if (!s_display) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

lv_display_t *lcd_get_display(void)
{
    return s_display;
}

esp_err_t lcd_brightness_set(uint8_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
    return ESP_OK;
}
