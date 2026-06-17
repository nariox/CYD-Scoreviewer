#include "wifi_backend.h"
#include "nvs_settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_backend";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_SCAN_DONE_BIT BIT2

static wifi_state_t s_state = WIFI_STATE_IDLE;
static int s_retry_num = 0;
static char s_ip_str[16] = "";
static char s_connected_ssid[33] = "";

static wifi_state_callback_t s_state_cb = NULL;
static void *s_state_cb_user_data = NULL;
static wifi_scan_callback_t s_scan_cb = NULL;
static void *s_scan_cb_user_data = NULL;

static SemaphoreHandle_t s_wifi_op_sem = NULL;

typedef enum {
    WIFI_OP_NONE,
    WIFI_OP_SCAN,
    WIFI_OP_CONNECT,
} wifi_op_t;

static wifi_op_t s_pending_op = WIFI_OP_NONE;

static nvs_wifi_credentials_t s_saved_creds;
static uint8_t s_current_network_idx = 0;
static bool s_creds_loaded = false;

static lv_timer_t *s_retry_timer = NULL;
static uint32_t s_retry_interval_ms = WIFI_BACKEND_RETRY_INTERVAL_MS;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data);

static void wifi_operation_task(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        xSemaphoreTake(s_wifi_op_sem, portMAX_DELAY);

        if (s_pending_op == WIFI_OP_SCAN) {
            s_state = WIFI_STATE_SCANNING;
            ESP_LOGI(TAG, "Starting scan...");

            esp_err_t scan_ret = esp_wifi_scan_start(NULL, true);
            ESP_LOGI(TAG, "esp_wifi_scan_start returned: %d", scan_ret);

            if (scan_ret != ESP_OK) {
                ESP_LOGE(TAG, "Scan failed: %d", scan_ret);
                s_state = WIFI_STATE_IDLE;
                if (s_scan_cb) {
                    wifi_scan_results_t empty;
                    memset(&empty, 0, sizeof(empty));
                    s_scan_cb(&empty, s_scan_cb_user_data);
                }
                continue;
            }

            wifi_scan_results_t results;
            memset(&results, 0, sizeof(results));

            uint16_t num = 0;
            esp_err_t ret = esp_wifi_scan_get_ap_num(&num);
            ESP_LOGI(TAG, "esp_wifi_scan_get_ap_num returned: %d, count=%u", ret, num);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get AP count: %d", ret);
                s_state = WIFI_STATE_IDLE;
                if (s_scan_cb) {
                    s_scan_cb(&results, s_scan_cb_user_data);
                }
                continue;
            }

            if (num > WIFI_BACKEND_MAX_AP) {
                num = WIFI_BACKEND_MAX_AP;
            }

            wifi_ap_record_t aps[WIFI_BACKEND_MAX_AP];
            memset(aps, 0, sizeof(aps));

            ret = esp_wifi_scan_get_ap_records(&num, aps);
            ESP_LOGI(TAG, "esp_wifi_scan_get_ap_records returned: %d, got=%u", ret, num);

            results.count = num;
            for (uint16_t i = 0; i < num; i++) {
                strncpy(results.entries[i].ssid, (const char *)aps[i].ssid, 32);
                results.entries[i].ssid[32] = '\0';
                results.entries[i].rssi = aps[i].rssi;
                results.entries[i].channel = aps[i].primary;
                results.entries[i].auth = aps[i].authmode;
            }

            esp_wifi_clear_ap_list();
            s_state = WIFI_STATE_IDLE;

            if (s_scan_cb) {
                s_scan_cb(&results, s_scan_cb_user_data);
            }

        } else if (s_pending_op == WIFI_OP_CONNECT) {
            EventBits_t bits = xEventGroupWaitBits(
                s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdTRUE,
                pdFALSE,
                pdMS_TO_TICKS(30000)
            );

            if (bits & WIFI_FAIL_BIT) {
                if (s_state_cb) {
                    s_state_cb(s_state, s_state_cb_user_data);
                }
            }
        }

        s_pending_op = WIFI_OP_NONE;
    }
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                break;
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event =
                    (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "Disconnected: reason=%d", event->reason);

                if (s_state == WIFI_STATE_CONNECTING && s_retry_num < WIFI_BACKEND_MAX_RETRY) {
                    esp_err_t ret = esp_wifi_connect();
                    if (ret == ESP_OK) {
                        s_retry_num++;
                        ESP_LOGI(TAG, "Reconnect attempt %d", s_retry_num);
                    } else {
                        ESP_LOGE(TAG, "esp_wifi_connect retry failed: %d", ret);
                        s_state = WIFI_STATE_FAILED;
                        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    }
                } else if (s_state == WIFI_STATE_CONNECTING) {
                    s_state = WIFI_STATE_FAILED;
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    ESP_LOGE(TAG, "Connection failed after %d retries", s_retry_num);
                } else {
                    s_state = WIFI_STATE_DISCONNECTED;
                }
                break;
            }
            case WIFI_EVENT_SCAN_DONE:
                xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_DONE_BIT);
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            s_retry_num = 0;
            snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));

            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                strncpy(s_connected_ssid, (const char *)ap_info.ssid, 32);
                s_connected_ssid[32] = '\0';
            }

            s_state = WIFI_STATE_CONNECTED;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            ESP_LOGI(TAG, "Connected! IP: %s SSID: %s", s_ip_str, s_connected_ssid);

            if (s_retry_timer) {
                lv_timer_del(s_retry_timer);
                s_retry_timer = NULL;
            }

            if (s_state_cb) {
                s_state_cb(s_state, s_state_cb_user_data);
            }
        }
    }
}

static esp_err_t wifi_do_connect(const char *ssid, const char *password)
{
    s_pending_op = WIFI_OP_CONNECT;
    s_retry_num = 0;
    s_state = WIFI_STATE_CONNECTING;

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = {0},
            .password = {0},
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, ssid, 32);

    if (password && strlen(password) > 0) {
        strncpy((char *)wifi_config.sta.password, password, 64);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %d", ret);
        s_state = WIFI_STATE_FAILED;
        return ret;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %d", ret);
        s_state = WIFI_STATE_FAILED;
        return ret;
    }

    xSemaphoreGive(s_wifi_op_sem);
    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    return ESP_OK;
}

static void retry_timer_callback(lv_timer_t *timer)
{
    (void)timer;
    if (s_state == WIFI_STATE_CONNECTED) {
        return;
    }

    if (!s_creds_loaded || s_saved_creds.count == 0) {
        return;
    }

    ESP_LOGI(TAG, "Retry timer: trying network %u/%u", s_current_network_idx + 1, s_saved_creds.count);

    nvs_wifi_network_t *net = &s_saved_creds.networks[s_current_network_idx];
    if (strlen(net->ssid) == 0) {
        s_current_network_idx = (s_current_network_idx + 1) % s_saved_creds.count;
        return;
    }

    wifi_do_connect(net->ssid, net->password);
    s_current_network_idx = (s_current_network_idx + 1) % s_saved_creds.count;
}

esp_err_t wifi_backend_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }

    s_wifi_op_sem = xSemaphoreCreateBinary();
    if (!s_wifi_op_sem) {
        vEventGroupDelete(s_wifi_event_group);
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());

    bool event_loop_exists = false;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        event_loop_exists = true;
        ESP_LOGI(TAG, "Event loop already exists, skipping creation");
    }

    if (!event_loop_exists) {
        ESP_ERROR_CHECK(esp_event_loop_create_default());
    }

    if (!netif) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    BaseType_t ret = xTaskCreate(wifi_operation_task, "wifi_op", 4096,
                                  NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi operation task");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_saved_creds, 0, sizeof(s_saved_creds));
    nvs_settings_load_wifi_networks(&s_saved_creds);
    s_creds_loaded = true;

    s_state = WIFI_STATE_INITIALIZING;
    ESP_LOGI(TAG, "WiFi backend initialized, %u saved network(s)", s_saved_creds.count);

    esp_err_t start_ret = esp_wifi_start();
    if (start_ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %d", start_ret);
        return start_ret;
    }
    s_state = WIFI_STATE_IDLE;
    return ESP_OK;
}

esp_err_t wifi_backend_start(void)
{
    esp_err_t start_ret = esp_wifi_start();
    if (start_ret == ESP_OK) {
        s_state = WIFI_STATE_IDLE;
    }
    return start_ret;
}

esp_err_t wifi_backend_stop(void)
{
    wifi_backend_stop_retry_timer();
    return esp_wifi_stop();
}

esp_err_t wifi_backend_deinit(void)
{
    wifi_backend_stop_retry_timer();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);
    vSemaphoreDelete(s_wifi_op_sem);
    vEventGroupDelete(s_wifi_event_group);
    s_state = WIFI_STATE_IDLE;
    return ESP_OK;
}

wifi_state_t wifi_backend_get_state(void)
{
    return s_state;
}

const char *wifi_backend_get_ip(void)
{
    return s_state == WIFI_STATE_CONNECTED ? s_ip_str : "";
}

const char *wifi_backend_get_connected_ssid(void)
{
    return s_connected_ssid;
}

esp_err_t wifi_backend_scan(wifi_scan_callback_t cb, void *user_data)
{
    s_scan_cb = cb;
    s_scan_cb_user_data = user_data;
    s_pending_op = WIFI_OP_SCAN;
    xSemaphoreGive(s_wifi_op_sem);
    return ESP_OK;
}

esp_err_t wifi_backend_connect(const char *ssid, const char *password)
{
    if (s_state == WIFI_STATE_CONNECTED) {
        esp_wifi_disconnect();
    }

    uint8_t existing_idx;
    esp_err_t found = nvs_settings_find_wifi_network(ssid, &existing_idx);

    if (found != ESP_ERR_NOT_FOUND) {
        nvs_wifi_network_t *net = &s_saved_creds.networks[existing_idx];
        strncpy(net->ssid, ssid, NVS_MAX_SSID_LEN - 1);
        if (password) {
            strncpy(net->password, password, NVS_MAX_PWD_LEN - 1);
        }
        nvs_settings_save_wifi_network(existing_idx, ssid, password);
        s_current_network_idx = existing_idx;
    } else {
        uint8_t idx = s_saved_creds.count;
        if (idx >= NVS_MAX_WIFI_NETWORKS) {
            idx = s_current_network_idx;
        }
        nvs_settings_save_wifi_network(idx, ssid, password);
        nvs_wifi_network_t *net = &s_saved_creds.networks[idx];
        strncpy(net->ssid, ssid, NVS_MAX_SSID_LEN - 1);
        if (password) {
            strncpy(net->password, password, NVS_MAX_PWD_LEN - 1);
        }
        if (idx >= s_saved_creds.count) {
            s_saved_creds.count = idx + 1;
        }
        s_current_network_idx = idx;
    }

    wifi_backend_stop_retry_timer();
    return wifi_do_connect(ssid, password);
}

esp_err_t wifi_backend_connect_next(void)
{
    if (!s_creds_loaded || s_saved_creds.count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    if (s_state == WIFI_STATE_CONNECTED) {
        esp_wifi_disconnect();
    }

    nvs_wifi_network_t *net = &s_saved_creds.networks[s_current_network_idx];
    if (strlen(net->ssid) == 0) {
        s_current_network_idx = (s_current_network_idx + 1) % s_saved_creds.count;
        net = &s_saved_creds.networks[s_current_network_idx];
    }

    wifi_backend_stop_retry_timer();
    esp_err_t ret = wifi_do_connect(net->ssid, net->password);
    s_current_network_idx = (s_current_network_idx + 1) % s_saved_creds.count;
    return ret;
}

esp_err_t wifi_backend_disconnect(void)
{
    esp_wifi_disconnect();
    s_state = WIFI_STATE_DISCONNECTED;
    strcpy(s_ip_str, "");
    strcpy(s_connected_ssid, "");
    return ESP_OK;
}

esp_err_t wifi_backend_autoconnect(void)
{
    if (!s_creds_loaded || s_saved_creds.count == 0) {
        ESP_LOGW(TAG, "No saved credentials for autoconnect");
        return ESP_ERR_NOT_FOUND;
    }

    s_current_network_idx = 0;
    nvs_wifi_network_t *net = &s_saved_creds.networks[0];
    ESP_LOGI(TAG, "Auto-connecting to: %s", net->ssid);

    return wifi_do_connect(net->ssid, net->password);
}

esp_err_t wifi_backend_start_retry_timer(uint32_t interval_ms)
{
    if (interval_ms == 0) {
        interval_ms = WIFI_BACKEND_RETRY_INTERVAL_MS;
    }
    s_retry_interval_ms = interval_ms;

    if (s_retry_timer) {
        lv_timer_del(s_retry_timer);
    }

    s_retry_timer = lv_timer_create(retry_timer_callback, interval_ms, NULL);
    if (!s_retry_timer) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Retry timer started (%ums interval)", interval_ms);
    return ESP_OK;
}

esp_err_t wifi_backend_stop_retry_timer(void)
{
    if (s_retry_timer) {
        lv_timer_del(s_retry_timer);
        s_retry_timer = NULL;
        ESP_LOGI(TAG, "Retry timer stopped");
    }
    return ESP_OK;
}

bool wifi_backend_is_retrying(void)
{
    return s_retry_timer != NULL;
}

void wifi_backend_register_state_callback(wifi_state_callback_t cb, void *user_data)
{
    s_state_cb = cb;
    s_state_cb_user_data = user_data;
}

void wifi_backend_register_scan_callback(wifi_scan_callback_t cb, void *user_data)
{
    s_scan_cb = cb;
    s_scan_cb_user_data = user_data;
}

uint8_t wifi_backend_get_current_network_index(void)
{
    return s_current_network_idx;
}

void wifi_backend_set_current_network_index(uint8_t index)
{
    if (index < s_saved_creds.count) {
        s_current_network_idx = index;
    }
}
