#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "nba_api.h"

/* College scores from ESPN's public scoreboard (site.api.espn.com) - the free
   source for this, balldontlie paywalls its college games (see TODO.md).

   Coverage caveat, measured against ESPN's own pages: during the regular
   season this feed carries only the network-covered slate (~10-20 games a
   day, the marquee matchups), while the NCAA tournament days come in full.
   The board shows that slate, not every D1 game. */

#define ESPN_API_MAX_GAMES 24

/* Fetch one US game day ("YYYY-MM-DD", same day semantics as balldontlie) of
   a college league (LEAGUE_NCAAB / LEAGUE_NCAAW). Fills up to max_games rows
   into out, count in *out_count; order unspecified (the service sorts).
   ESPN asks for a team id + logo per game, and its own scoreboard pads each
   event heavily - see the streaming parser in espn_api.c. */
esp_err_t espn_api_fetch_games(league_t league, const char *day_iso,
                               nba_game_t *out, int max_games, int *out_count);

/* Parse a complete scoreboard body. Split out from the fetch so the host
   preview harness (tools/sim) can run the very scanner the device streams
   through, against saved fixtures. */
esp_err_t espn_parse_scoreboard(const char *body, size_t len, league_t league,
                                nba_game_t *out, int max_games, int *out_count);
