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
#include "nba_api.h"
#include "time_sync.h"
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

static bool s_boot_connected = false;
static bool s_nba_demo_started = false;

static void splash_timer_cb(lv_timer_t *timer);
static void wifi_state_update_timer(lv_timer_t *timer);
static void start_splash_flow(void);

/* NBA API sample: syncs the clock, fetches the last 7 days of games and
   prints them to the console. One-shot per boot; remove once the cards
   screen shows live data. */
static void nba_demo_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "NBA demo: syncing clock...");
    if (time_sync_from_http() != ESP_OK) {
        ESP_LOGW(TAG, "NBA demo: clock not synced, using epoch-relative dates");
    }

    char end_date[16], start_date[16];
    if (time_sync_date_str(end_date, sizeof(end_date), 0) != ESP_OK ||
        time_sync_date_str(start_date, sizeof(start_date), 6) != ESP_OK) {
        /* Clock not synced: fall back to a window around the balldontlie
           sample period so the fetch itself still exercises the API. */
        strcpy(end_date, "2026-10-17");
        strcpy(start_date, "2026-10-11");
    }

    static nba_game_t games[NBA_API_MAX_GAMES];
    int count = 0;

    esp_err_t err = nba_api_fetch_games(start_date, end_date, NULL,
                                        games, NBA_API_MAX_GAMES, &count);
    if (err == ESP_OK && count == 0) {
        /* Mid-October is preseason, which the default query excludes. */
        err = nba_api_fetch_games(start_date, end_date, "preseason",
                                  games, NBA_API_MAX_GAMES, &count);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NBA demo: fetch failed: %s", esp_err_to_name(err));
    } else {
        nba_api_log_games(games, count);
    }

    ESP_LOGI(TAG, "NBA demo done (free heap: %u)", esp_get_free_heap_size());
    vTaskDelete(NULL);
}

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

        if (!s_nba_demo_started) {
            s_nba_demo_started = true;
            if (xTaskCreate(nba_demo_task, "nba_demo", 8192, NULL, 3, NULL) != pdPASS) {
                ESP_LOGE(TAG, "NBA demo: failed to create task");
            }
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
