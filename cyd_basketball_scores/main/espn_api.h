#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "nba_api.h"

/* All four score feeds from ESPN's public scoreboard (site.api.espn.com):
   NBA / WNBA / NCAAB / NCAAW are the same site API and the same JSON shape,
   fetched through one streaming scanner (constant memory - a covered day is
   200-400 KB of padded JSON, a cJSON tree this heap would die on). No API
   key, no published quota - still pace politely (ESPN_API_MIN_GAP_MS).

   Coverage: the pro scoreboards carry the FULL league slate. College carries
   only the network-covered games (~10-20/day in conference season; full
   tournament days), which is a property of ESPN's college feed, not ours.

   Team ids: college games carry ESPN school ids (the NCAA logo table's key).
   Pro games arrive with ESPN's own team ids, which espn_api re-maps to the
   balldontlie namespace the baked logo/arena tables use (pro_team_ids.h). */

#define ESPN_API_MAX_GAMES 24

/* Fetch one US game day ("YYYY-MM-DD") of any league. Fills up to max_games
   rows into out, count in *out_count; order unspecified (the service sorts).
   ESPN pads each event heavily and asks for a team id + logo per game - see
   the streaming parser in espn_api.c. */
esp_err_t espn_api_fetch_games(league_t league, const char *day_iso,
                               nba_game_t *out, int max_games, int *out_count);

/* Parse a complete scoreboard body. Split out from the fetch so the host
   preview harness (tools/sim) can run the very scanner the device streams
   through, against saved fixtures. Pro fixtures run through the same
   id remap the device fetch does. */
esp_err_t espn_parse_scoreboard(const char *body, size_t len, league_t league,
                                nba_game_t *out, int max_games, int *out_count);

/* Hand back the keep-alive connection (call when the feed goes idle). An
   open TLS session parks ~22 KB of record buffers + context — exactly the
   pool the next handshake needs, so the reuse is scoped to a burst (day
   walk, pre-warm, slate hunt: requests seconds apart) and nothing is held
   across the refresh gap. Cheap to call; safe only from the feed worker
   (all espn_api callers are that one task). */
void espn_api_conn_release(void);
