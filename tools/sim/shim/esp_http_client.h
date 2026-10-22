/* Host shim: just enough of esp_http_client for espn_api.c to compile and
   link. The preview harness never fetches (no network); it calls the public
   espn_parse_scoreboard entry point instead, so every stub here only has to
   keep the linker happy. Field names mirror the real struct enough that
   espn_api.c's designated initializers compile unchanged. */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef void *esp_http_client_handle_t;

typedef enum {
    HTTP_METHOD_GET = 0,
} esp_http_client_method_t;

typedef enum {
    HTTP_EVENT_ON_DATA = 4,
} esp_http_client_event_id_t;

typedef struct {
    esp_http_client_event_id_t event_id;
    void *user_data;
    const char *data;
    int data_len;
} esp_http_client_event_t;

typedef esp_err_t (*http_event_cb_t)(esp_http_client_event_t *evt);

typedef struct {
    const char *url;
    esp_http_client_method_t method;
    esp_err_t (*crt_bundle_attach)(void *cfg);
    http_event_cb_t event_handler;
    void *user_data;
    int timeout_ms;
    int buffer_size;
    bool keep_alive_enable;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char *key, const char *value);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
