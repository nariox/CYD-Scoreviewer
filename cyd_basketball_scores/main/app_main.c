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
#include "game_detail_screen.h"
#include "settings_screen.h"
#include "wifi_screen.h"
#include "wifi_backend.h"
#include "nba_service.h"
#include "touch_integration.h"
#include "calibration_screen.h"
#include "nvs_settings.h"

static const char *TAG = "cyd_scores";

static lv_obj_t *splash_scr;
static lv_obj_t *cards_scr;
static lv_obj_t *settings_scr;
static lv_obj_t *wifi_scr;
static lv_obj_t *calibration_scr;

static bool s_boot_connected = false;
static bool s_feed_started = false;

static void splash_timer_cb(lv_timer_t *timer);
static void wifi_state_update_timer(lv_timer_t *timer);
static void start_splash_flow(void);

static void calibration_done(void)
{
    nvs_settings_save_calibration_data(true);
    start_splash_flow();
}

static void start_splash_flow(void)
{
    lv_scr_load(splash_scr);
    lv_timer_t *splash_timer = lv_timer_create(splash_timer_cb, 5000, NULL);
    lv_timer_set_repeat_count(splash_timer, 1);
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
        ESP_LOGI(TAG, "No WiFi yet, loading cards screen, arming retry timer");
        cards_screen_update_wifi_status(state);
        lv_scr_load(cards_scr);
        wifi_backend_start_retry_timer(0);
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

        if (!s_feed_started) {
            s_feed_started = true;
            nba_service_start();
        }
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

    /* The blob was saved AND loaded, but nothing handed it to the touch
       handler - every reboot quietly ran on the 0..4095 defaults until the
       user recalibrated, so calibration looked unsaved. */
    if (settings.calibration_saved) {
        touch_integration_apply_calibration(&settings.calibration);
        ESP_LOGI(TAG, "Applied saved calibration to touch (swap_xy=%d)",
                 (int)settings.calibration.swap_xy);
    }

    ESP_LOGI(TAG, "Init WiFi backend");
    ESP_ERROR_CHECK(wifi_backend_init());
    wifi_backend_register_state_callback(wifi_state_ui_callback, NULL);

    ESP_LOGI(TAG, "Create screens");
    /* LVGL calls from app_main must hold the port lock: the esp_lvgl_port
       task renders concurrently, and unserialized screen creation races
       with it (intermittent LoadProhibited panics). */
    lvgl_port_lock(0);
    splash_scr = splash_screen_create();
    cards_scr = cards_screen_create();
    settings_scr = settings_screen_create();
    wifi_scr = wifi_screen_create();
    calibration_scr = calibration_screen_create();
    /* touch_test_screen is intentionally not created: every screen lives in
       LVGL's fixed 64KB pool, and its 9 buttons + labels cost headroom the
       score cards need. Build it into a debug image when needed. */

    ESP_LOGI(TAG, "Setting navigation");
    {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        ESP_LOGI(TAG, "lv pool after screens: %u%% used, %u of %u bytes free",
                 mon.used_pct, (unsigned)mon.free_size, (unsigned)mon.total_size);
    }
    cards_screen_set_settings_scr(settings_scr);
    /* The detail screen builds its widgets on the first card tap (LVGL pool is
       scarce), so only its back target is wired here. */
    game_detail_screen_set_back_scr(cards_scr);
    settings_screen_set_wifi_scr(wifi_scr);
    settings_screen_set_cards_scr(cards_scr);
    settings_screen_set_calibration_scr(calibration_scr);
    wifi_screen_set_settings_scr(settings_scr);
    wifi_screen_set_cards_scr(cards_scr);
    calibration_screen_set_done_cb(calibration_done);
    calibration_screen_set_back_scr(settings_scr);

    if (settings.calibration_saved) {
        ESP_LOGI(TAG, "Calibration found, starting splash + autoconnect");
        s_boot_connected = true;
        start_splash_flow();
    } else {
        ESP_LOGI(TAG, "No calibration found, loading calibration screen");
        lv_scr_load(calibration_scr);
    }
    lvgl_port_unlock();

    /* Autoconnect outside the port lock: state-change callbacks take it. */
    if (s_boot_connected) {
        wifi_backend_autoconnect();
        s_boot_connected = false;
    }

    ESP_LOGI(TAG, "Initialization complete");
}
