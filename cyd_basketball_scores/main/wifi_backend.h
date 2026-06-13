#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#define WIFI_BACKEND_MAX_AP 20
#define WIFI_BACKEND_MAX_RETRY 5
#define WIFI_BACKEND_RETRY_INTERVAL_MS 60000

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_INITIALIZING,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
    WIFI_STATE_DISCONNECTED,
} wifi_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t auth;
} wifi_ap_entry_t;

typedef struct {
    wifi_ap_entry_t entries[WIFI_BACKEND_MAX_AP];
    uint16_t count;
} wifi_scan_results_t;

typedef void (*wifi_state_callback_t)(wifi_state_t new_state, void *user_data);
typedef void (*wifi_scan_callback_t)(wifi_scan_results_t *results, void *user_data);

esp_err_t wifi_backend_init(void);
esp_err_t wifi_backend_start(void);
esp_err_t wifi_backend_stop(void);
esp_err_t wifi_backend_deinit(void);

wifi_state_t wifi_backend_get_state(void);
const char *wifi_backend_get_ip(void);
const char *wifi_backend_get_connected_ssid(void);

esp_err_t wifi_backend_scan(wifi_scan_callback_t cb, void *user_data);
esp_err_t wifi_backend_connect(const char *ssid, const char *password);
esp_err_t wifi_backend_connect_next(void);
esp_err_t wifi_backend_disconnect(void);

esp_err_t wifi_backend_autoconnect(void);
esp_err_t wifi_backend_start_retry_timer(uint32_t interval_ms);
esp_err_t wifi_backend_stop_retry_timer(void);
bool wifi_backend_is_retrying(void);

void wifi_backend_register_state_callback(wifi_state_callback_t cb, void *user_data);
void wifi_backend_register_scan_callback(wifi_scan_callback_t cb, void *user_data);

uint8_t wifi_backend_get_current_network_index(void);
void wifi_backend_set_current_network_index(uint8_t index);
