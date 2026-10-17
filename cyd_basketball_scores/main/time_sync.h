#pragma once

#include <stddef.h>
#include "esp_err.h"

/* Sync the system clock from the HTTP Date header of a small HTTPS GET.
   Low accuracy (second-level + request latency) but no NTP/UDP dependency. */
esp_err_t time_sync_from_http(void);

/* True once time_sync_from_http() has succeeded. */
int time_sync_is_synced(void);

/* Write current date shifted by days_ago days as "YYYY-MM-DD" (UTC-based).
   Returns ESP_ERR_INVALID_STATE if the clock was never synced. */
esp_err_t time_sync_date_str(char *buf, size_t len, int days_ago);
