#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "touch_integration.h"

/* NVS namespace */
#define NVS_SETTINGS_NS "cyd_scores"

/* NVS keys */
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_CALIBRATION "calibration"
#define NVS_KEY_CALIBRATION_SAVED "cal_saved"
#define NVS_KEY_LAST_SCREEN "last_screen"

/* Screen identifiers for NVS persistence */
#define LAST_SCREEN_CALIBRATION  0
#define LAST_SCREEN_SPLASH       1
#define LAST_SCREEN_CARDS        2
#define LAST_SCREEN_SETTINGS     3
#define LAST_SCREEN_WIFI         4

/* NVS settings container */
typedef struct {
    uint8_t brightness;
    calibration_data_t calibration;
    bool calibration_saved;
    uint8_t last_screen;
} nvs_settings_t;

/* Initialize NVS (called once at boot) */
esp_err_t nvs_settings_init(void);

/* Save individual settings */
esp_err_t nvs_settings_save_brightness(uint8_t brightness);
esp_err_t nvs_settings_save_calibration(const calibration_data_t *cal);
esp_err_t nvs_settings_save_calibration_data(bool saved);
esp_err_t nvs_settings_save_last_screen(uint8_t screen_id);

/* Load individual settings */
esp_err_t nvs_settings_load_brightness(uint8_t *brightness);
esp_err_t nvs_settings_load_calibration(calibration_data_t *cal);
esp_err_t nvs_settings_load_last_screen(uint8_t *screen_id);

/* Load all settings at once */
esp_err_t nvs_settings_load_all(nvs_settings_t *settings);

/* Reset all settings to defaults */
esp_err_t nvs_settings_reset(void);
