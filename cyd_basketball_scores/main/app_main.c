#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "hardware.h"
#include "lcd.h"
#include "splash_screen.h"
#include "cards_screen.h"
#include "settings_screen.h"
#include "wifi_screen.h"
#include "touch_integration.h"
#include "touch_test_screen.h"
#include "calibration_screen.h"

static const char *TAG = "cyd_scores";

static lv_obj_t *splash_scr;
static lv_obj_t *cards_scr;
static lv_obj_t *settings_scr;
static lv_obj_t *wifi_scr;
static lv_obj_t *touch_test_scr;

void app_main(void)
{
    ESP_LOGI(TAG, "CYD Basketball Scores - Starting");

    /* Initialize LCD (SPI, panel, backlight, LVGL) */
    ESP_ERROR_CHECK(lcd_init());
    lv_display_t *display = lcd_get_display();

    /* Create screens */
    splash_scr = splash_screen_create();
    cards_scr = cards_screen_create();
    settings_scr = settings_screen_create();
    wifi_scr = wifi_screen_create();
    touch_test_scr = touch_test_screen_create();

    /* Wire navigation */
    cards_screen_set_settings_scr(settings_scr);
    settings_screen_set_wifi_scr(wifi_scr);
    settings_screen_set_cards_scr(cards_scr);
    wifi_screen_set_settings_scr(settings_scr);

    /* Load initial screen */
    lv_scr_load(touch_test_scr);

    /* Initialize touch */
    esp_lcd_touch_handle_t tp;
    lvgl_port_touch_cfg_t touch_cfg;
    ESP_ERROR_CHECK(touch_integration_init(&tp));
    touch_cfg.disp = display;
    touch_cfg.handle = tp;
    touch_cfg.scale.x = 0;
    touch_cfg.scale.y = 0;
    lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "Initialization complete");
}
