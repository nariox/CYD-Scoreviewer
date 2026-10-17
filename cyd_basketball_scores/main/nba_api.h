#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Cap on parsed games per fetch call. */
#define NBA_API_MAX_GAMES 32

/* API key compiled in via defaults_override.h (NBA_API_KEY). Can be
   replaced at runtime with nba_api_set_api_key(). */
#ifndef NBA_API_KEY
#define NBA_API_KEY ""
#endif

typedef struct {
    int  id;
    char date[12];            /* "YYYY-MM-DD" (US-based game date) */
    char datetime[26];        /* ISO 8601 UTC, e.g. "2026-10-11T00:00:00.000Z" */
    char status[24];          /* raw status, e.g. "Final", "Scheduled" */
    char status_state[16];    /* scheduled | in_progress | final | postponed | canceled | ... */
    char time[16];            /* live clock/status text, e.g. "Final", "1:00" */
    int  period;              /* current/last period, -1 if absent */
    bool postseason;
    bool postponed;
    int  home_score;          /* -1 when not available (game not played) */
    int  visitor_score;       /* -1 when not available */
    char home_abbr[8];
    char home_name[48];
    char visitor_abbr[8];
    char visitor_name[48];
} nba_game_t;

/* Override the API key at runtime (copied into internal storage). */
void nba_api_set_api_key(const char *key);

/* Fetch games with start_date <= date <= end_date ("YYYY-MM-DD").
   season_type: NULL (all non-preseason types), or one of
   "preseason"|"regular"|"ist"|"playin"|"playoffs".
   Fills up to max_games entries into out, count in *out_count. */
esp_err_t nba_api_fetch_games(const char *start_date, const char *end_date,
                              const char *season_type,
                              nba_game_t *out, int max_games, int *out_count);

/* Log fetched games to the console (demo/debug aid). */
void nba_api_log_games(const nba_game_t *games, int count);
