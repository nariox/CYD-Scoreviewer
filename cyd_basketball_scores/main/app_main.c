#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <esp_lvgl_port.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

#include "splash_screen.h"
#include "cards_screen.h"
#include "settings_screen.h"
#include "wifi_screen.h"
#include "touch_integration.h"
#include "touch_test_screen.h"
#include "calibration_screen.h"

static const char *TAG = "cyd_scores";

#define LCD_HOST    SPI2_HOST
#define TOUCH_HOST  SPI3_HOST

#define PIN_LCD_BL      21
#define PIN_LCD_DC      2
#define PIN_LCD_RST     4
#define PIN_LCD_CS      15
#define PIN_LCD_MOSI    13
#define PIN_LCD_MISO    12
#define PIN_LCD_SCLK    14
#define PIN_TOUCH_MOSI  32
#define PIN_TOUCH_MISO  39
#define PIN_TOUCH_SCLK  25
#define PIN_TOUCH_CS    33
#define PIN_TOUCH_IRQ   36

#define LCD_H_RES       320
#define LCD_V_RES       240

#define LCD_BL_ON_LEVEL 1
#define LCD_BL_OFF_LEVEL !LCD_BL_ON_LEVEL

#define LCD_CMD_BITS    8
#define LCD_PARAM_BITS  8

#define LVGL_DRAW_BUF_LINES    20
#define LVGL_TICK_PERIOD_MS    2
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ
#define LVGL_TASK_STACK_SIZE   (8 * 1024)
#define LVGL_TASK_PRIORITY     2

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        0
#define LEDC_FREQ_HZ        (5000)

static _lock_t lvgl_api_lock;

static lv_display_t *display;
static lv_obj_t *splash_scr;
static lv_obj_t *cards_scr;
static lv_obj_t *settings_scr;
static lv_obj_t *wifi_scr;
static lv_obj_t *touch_test_scr;

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

static void switch_to_cards_screen(lv_timer_t *timer)
{
    lv_scr_load(cards_scr);
}

void app_main(void)
{
    ESP_LOGI(TAG, "CYD Basketball Scores - Starting");

    ESP_LOGI(TAG, "Initialize backlight LEDC PWM");
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = LEDC_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PIN_LCD_BL,
        .duty           = 128,
        .hpoint         = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ESP_LOGI(TAG, "Initialize LVGL");
    //lv_init();
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t err = lvgl_port_init(&lvgl_cfg);

    ESP_LOGI(TAG, "Initialize SPI bus");
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install ST7789 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES*LCD_V_RES/32,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rounder_cb = NULL,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        }
    };
    display = lvgl_port_add_disp(&disp_cfg);

    ESP_LOGI(TAG, "Create screens");
    _lock_acquire(&lvgl_api_lock);

    splash_scr = splash_screen_create();
    cards_scr = cards_screen_create();
    settings_scr = settings_screen_create();
    wifi_scr = wifi_screen_create();
    touch_test_scr = touch_test_screen_create();

    cards_screen_set_settings_scr(settings_scr);
    settings_screen_set_wifi_scr(wifi_scr);
    settings_screen_set_cards_scr(cards_scr);
    wifi_screen_set_settings_scr(settings_scr);

    lv_scr_load(touch_test_scr);
    _lock_release(&lvgl_api_lock);

    ESP_LOGI(TAG, "Initialize touch");
    esp_lcd_touch_handle_t tp;
    lvgl_port_touch_cfg_t touch_cfg;
    ESP_ERROR_CHECK(touch_integration_init(&tp, TOUCH_HOST, PIN_TOUCH_MOSI, PIN_TOUCH_MISO, PIN_TOUCH_SCLK, PIN_TOUCH_CS, PIN_TOUCH_IRQ));
    touch_cfg.disp = display;
    touch_cfg.handle = tp;
    touch_cfg.scale.x = 0;
    touch_cfg.scale.y = 0;
    lvgl_port_add_touch(&touch_cfg);

//    ESP_LOGI(TAG, "Create LVGL task");
//    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Initialization complete");
}
