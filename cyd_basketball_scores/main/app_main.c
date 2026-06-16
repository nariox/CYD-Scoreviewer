#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "themes/default/lv_theme_default.h"

#include "hardware.h"
#include "lcd.h"
#include "splash_screen.h"
#include "cards_screen.h"
#include "settings_screen.h"
#include "wifi_screen.h"
#include "wifi_backend.h"
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

static void wifi_state_update_timer(lv_timer_t *timer);

static void calibration_done(void)
{
    nvs_settings_save_calibration_data(true);
    lv_scr_load(splash_scr);
}

static void splash_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    wifi_state_t state = wifi_backend_get_state();

    if (state == WIFI_STATE_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected, loading cards screen");
        cards_screen_update_wifi_status(state);
        lv_scr_load(cards_scr);
    } else if (state == WIFI_STATE_FAILED) {
        ESP_LOGI(TAG, "WiFi failed, loading wifi screen");
        lv_scr_load(wifi_scr);
    } else {
        ESP_LOGI(TAG, "No WiFi credentials, loading cards screen");
        lv_scr_load(cards_scr);
    }
}

static void wifi_state_ui_callback(wifi_state_t new_state, void *user_data)
{
    (void)user_data;
    lv_timer_t *t = lv_timer_create((lv_timer_cb_t)wifi_state_update_timer, 0, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void wifi_state_update_timer(lv_timer_t *timer)
{
    (void)timer;
    wifi_state_t state = wifi_backend_get_state();

    if (cards_scr) {
        cards_screen_update_wifi_status(state);
    }

    if (state == WIFI_STATE_CONNECTED) {
        const char *ip = wifi_backend_get_ip();
        ESP_LOGI(TAG, "Connected to %s, IP: %s",
                 wifi_backend_get_connected_ssid(), ip ? ip : "pending");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CYD Basketball Scores - Starting");

    esp_log_level_set("spi_master", ESP_LOG_WARN);

    ESP_ERROR_CHECK(nvs_settings_init());
    ESP_ERROR_CHECK(lcd_init());
    lv_display_t *display = lcd_get_display();

    lv_theme_default_init(display,
                           lv_palette_lighten(LV_PALETTE_BLUE, 2),
                           lv_palette_darken(LV_PALETTE_BLUE, 3),
                           true,
                           LV_FONT_DEFAULT);

    ESP_LOGI(TAG, "Initing Touch");
    esp_lcd_touch_handle_t tp;
    lvgl_port_touch_cfg_t touch_cfg;
    ESP_ERROR_CHECK(touch_integration_init(&tp));
    touch_cfg.disp = display;
    touch_cfg.handle = tp;
    touch_cfg.scale.x = 0;
    touch_cfg.scale.y = 0;
    lvgl_port_add_touch(&touch_cfg);

    nvs_settings_t settings;
    nvs_settings_load_all(&settings);

    lcd_brightness_set(settings.brightness);
    ESP_LOGI(TAG, "Brightness: %u", settings.brightness);

    ESP_LOGI(TAG, "Calibration: x_min=%u x_max=%u y_min=%u y_max=%u swap_xy=%u saved=%s",
             settings.calibration.x_min, settings.calibration.x_max,
             settings.calibration.y_min, settings.calibration.y_max,
             settings.calibration.swap_xy, settings.calibration_saved ? "yes" : "no");

    ESP_LOGI(TAG, "Init WiFi backend");
    ESP_ERROR_CHECK(wifi_backend_init());
    wifi_backend_register_state_callback(wifi_state_ui_callback, NULL);

    ESP_LOGI(TAG, "Create screens");
    splash_scr = splash_screen_create();
    cards_scr = cards_screen_create();
    settings_scr = settings_screen_create();
    wifi_scr = wifi_screen_create();
    touch_test_scr = touch_test_screen_create();
    calibration_scr = calibration_screen_create();

    ESP_LOGI(TAG, "Setting navigation");
    cards_screen_set_settings_scr(settings_scr);
    settings_screen_set_wifi_scr(wifi_scr);
    settings_screen_set_cards_scr(cards_scr);
    settings_screen_set_calibration_scr(calibration_scr);
    wifi_screen_set_settings_scr(settings_scr);
    wifi_screen_set_cards_scr(cards_scr);
    calibration_screen_set_done_cb(calibration_done);
    calibration_screen_set_back_scr(settings_scr);

    if (settings.calibration_saved) {
        ESP_LOGI(TAG, "Calibration found, starting splash + autoconnect");
        lv_scr_load(splash_scr);

        wifi_backend_autoconnect();

        lv_timer_t *splash_timer = lv_timer_create(splash_timer_cb, 5000, NULL);
        lv_timer_set_repeat_count(splash_timer, 1);
    } else {
        ESP_LOGI(TAG, "No calibration found, loading calibration screen");
        lv_scr_load(calibration_scr);
    }

    ESP_LOGI(TAG, "Initialization complete");
}
