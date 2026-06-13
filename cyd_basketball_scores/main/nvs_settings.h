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

/* WiFi credential NVS keys (up to 3 saved networks) */
#define NVS_MAX_WIFI_NETWORKS 3
#define NVS_MAX_SSID_LEN 33
#define NVS_MAX_PWD_LEN 65

#define NVS_KEY_WIFI_SSID_0 "wifi_ssid_0"
#define NVS_KEY_WIFI_PWD_0 "wifi_pwd_0"
#define NVS_KEY_WIFI_SSID_1 "wifi_ssid_1"
#define NVS_KEY_WIFI_PWD_1 "wifi_pwd_1"
#define NVS_KEY_WIFI_SSID_2 "wifi_ssid_2"
#define NVS_KEY_WIFI_PWD_2 "wifi_pwd_2"
#define NVS_KEY_WIFI_COUNT "wifi_count"

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

/* WiFi credential type */
typedef struct {
    char ssid[NVS_MAX_SSID_LEN];
    char password[NVS_MAX_PWD_LEN];
} nvs_wifi_network_t;

typedef struct {
    nvs_wifi_network_t networks[NVS_MAX_WIFI_NETWORKS];
    uint8_t count;
} nvs_wifi_credentials_t;

/* WiFi credential NVS functions */
esp_err_t nvs_settings_save_wifi_network(uint8_t index, const char *ssid, const char *password);
esp_err_t nvs_settings_load_wifi_networks(nvs_wifi_credentials_t *creds);
esp_err_t nvs_settings_delete_wifi_network(uint8_t index);
esp_err_t nvs_settings_clear_all_wifi_networks(void);
esp_err_t nvs_settings_find_wifi_network(const char *ssid, uint8_t *index);
