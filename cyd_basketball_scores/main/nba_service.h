#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "nba_api.h"

/* NBA score feed worker: fetches a recent window of games and pushes them to
   the cards screen. Starts with nba_service_start() (call once, after the
   Wi-Fi backend is up), refreshes every NBA_SERVICE_REFRESH_MS, and can be
   poked early with nba_service_request_refresh() from anywhere. */
#ifndef NBA_SERVICE_REFRESH_MS
#define NBA_SERVICE_REFRESH_MS (15 * 60 * 1000)
#endif

void nba_service_start(void);
void nba_service_request_refresh(void);
bool nba_service_is_running(void);

/* Redraw the current page from the cache without any HTTP. The cards screen
   uses it after the game detail view borrowed the LVGL pool. */
void nba_service_rerender(void);

/* The board shows a fixed number of games per page (not one per day: a busy
   slate spans several pages, an off-day pulls several days into one).
   delta > 0 goes to older games, delta < 0 to newer ones; each step that needs
   games we have not fetched yet costs one HTTPS call per day. */
void nba_service_step_page(int delta);

/* Which page is shown: 0 = the most recent games. */
int nba_service_page(void);
