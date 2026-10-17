/* Host stubs for the preview harness (tools/sim): the cards screen calls
   into the service on the refresh button, and the wifi status badge reads
   backend state. Neither runs offline. espn_api.c links against the HTTP
   stubs at the bottom (its parse entry point needs none of them). */
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nba_api.h"
#include "nba_service.h"

void nba_service_start(void) {}
void nba_service_request_refresh(void) {}
void nba_service_rerender(void) {}
void nba_service_step_page(int delta) { (void)delta; }
int nba_service_page(void) { return 0; }
bool nba_service_is_running(void) { return false; }

/* The header tabs actually switch which mock board renders, so keep a real
   value here (matches the device behaviour the tabs depend on). */
static league_t s_sim_league = LEAGUE_NBA;
void nba_service_set_league(league_t lg) { s_sim_league = lg; }
league_t nba_service_league(void) { return s_sim_league; }

/* ---- esp_timer / FreeRTOS ---- */

int64_t sim_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void vTaskDelay(TickType_t ticks) { (void)ticks; }

/* ---- esp_http_client (link-only: no network in the harness) ---- */

esp_err_t esp_crt_bundle_attach(void *cfg) { (void)cfg; return ESP_OK; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    (void)cfg;
    return NULL;                       /* fetch then bails with ESP_ERR_NO_MEM */
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char *key, const char *value)
{
    (void)client; (void)key; (void)value;
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    (void)client;
    return ESP_FAIL;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    (void)client;
    return 0;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    (void)client;
    return ESP_OK;
}
