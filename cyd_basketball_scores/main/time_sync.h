#pragma once

#include <time.h>
#include "esp_err.h"

/* Sync the system clock from the HTTP Date header of a small HTTPS GET.
   Low accuracy (second-level + request latency) but no NTP/UDP dependency. */
esp_err_t time_sync_from_http(void);

/* True once time_sync_from_http() has succeeded. */
int time_sync_is_synced(void);

/* Midnight UTC of the current NBA *game day*: balldontlie labels games with the
   US calendar date, and games tip off in the US evening, which is already the
   next UTC day - so the clock is shifted back 12 hours before taking the day.
   Returns 0 if the clock was never synced. */
time_t time_sync_game_day_epoch(void);
