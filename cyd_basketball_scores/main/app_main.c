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
#include "nvs_settings.h"

static const char *TAG = "cyd_scores";

static lv_obj_t *splash_scr;
static lv_obj_t *cards_scr;
static lv_obj_t *settings_scr;
static lv_obj_t *wifi_scr;
static lv_obj_t *touch_test_scr;
static lv_obj_t *calibration_scr;

static void calibration_done(void)
{
    nvs_settings_save_calibration_data(true);
    lv_scr_load(splash_scr);
}

static void splash_timer_cb(lv_timer_t *timer)
{
    lv_scr_load(cards_scr);
}

void app_main(void)
{
    ESP_LOGI(TAG, "CYD Basketball Scores - Starting");

    /* Suppress INFO logs from spi_master by setting its level to WARN */
    esp_log_level_set("spi_master", ESP_LOG_WARN);

    /* Initialize NVS */
    ESP_ERROR_CHECK(nvs_settings_init());

    /* Initialize LCD (SPI, panel, backlight, LVGL) */
    ESP_ERROR_CHECK(lcd_init());
    lv_display_t *display = lcd_get_display();

    /* Initialize touch */
    ESP_LOGI(TAG, "Initing Touch");
    esp_lcd_touch_handle_t tp;
    lvgl_port_touch_cfg_t touch_cfg;
    ESP_ERROR_CHECK(touch_integration_init(&tp));
    touch_cfg.disp = display;
    touch_cfg.handle = tp;
    touch_cfg.scale.x = 0;
    touch_cfg.scale.y = 0;
    lvgl_port_add_touch(&touch_cfg);

    /* Load NVS settings */
    nvs_settings_t settings;
    nvs_settings_load_all(&settings);

    /* Apply saved brightness */
    lcd_brightness_set(settings.brightness);
    ESP_LOGI(TAG, "Brightness: %u", settings.brightness);

    /* Log calibration values for NVS verification */
    ESP_LOGI(TAG, "Calibration: x_min=%u x_max=%u y_min=%u y_max=%u swap_xy=%u saved=%s",
             settings.calibration.x_min, settings.calibration.x_max,
             settings.calibration.y_min, settings.calibration.y_max,
             settings.calibration.swap_xy, settings.calibration_saved ? "yes" : "no");

    /* Create screens */
    ESP_LOGI(TAG, "Create screens");
    splash_scr = splash_screen_create();
    cards_scr = cards_screen_create();
    settings_scr = settings_screen_create();
    wifi_scr = wifi_screen_create();
    touch_test_scr = touch_test_screen_create();
    calibration_scr = calibration_screen_create();

    /* Wire navigation */
    ESP_LOGI(TAG, "Setting settings");
    cards_screen_set_settings_scr(settings_scr);
    settings_screen_set_wifi_scr(wifi_scr);
    settings_screen_set_cards_scr(cards_scr);
    settings_screen_set_calibration_scr(calibration_scr);
    wifi_screen_set_settings_scr(settings_scr);
    calibration_screen_set_done_cb(calibration_done);
    calibration_screen_set_back_scr(settings_scr);

    /* Determine first screen */
    if (settings.calibration_saved) {
        ESP_LOGI(TAG, "Calibration found, loading splash");
        lv_scr_load(splash_scr);

        /* 3s splash → cards (one-shot) */
        lv_timer_t *splash_timer = lv_timer_create(splash_timer_cb, 3000, NULL);
        lv_timer_set_repeat_count(splash_timer, 1);
    } else {
        ESP_LOGI(TAG, "No calibration found, loading calibration screen");
        lv_scr_load(calibration_scr);
    }

    ESP_LOGI(TAG, "Initialization complete");
}
