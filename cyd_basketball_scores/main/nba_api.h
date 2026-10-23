#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Shared game record for the whole board - every screen, the service cache,
   the NVS snapshot, and the page boxes speak this struct. The data itself
   comes from ESPN's scoreboard for all four leagues (espn_api.h); the file
   name predates that migration and is kept because the type is everywhere.

   Team ids are per-league namespaces that overlap numerically (balldontlie
   NBA 1..30 / WNBA 1..13, ESPN school ids 94..112358), so every logo lookup
   keys off league_t - espn_api remaps pro ids to the balldontlie space the
   baked tables use (pro_team_ids.h), college keeps ESPN school ids. */

/* Which feed a game came from. Picks the logo table, the venue fallback,
   and the period conventions: NCAAB plays two halves, everyone else
   quarters. */
typedef enum {
    LEAGUE_NBA = 0,
    LEAGUE_WNBA,
    LEAGUE_NCAAB,
    LEAGUE_NCAAW,
} league_t;

typedef struct {
    int  id;
    char date[12];            /* US GAME DAY "YYYY-MM-DD" (event stamp - 12 h;
                                 evening tips are already tomorrow in UTC) */
    char datetime[26];        /* ISO 8601 UTC, e.g. "2026-10-11T00:00:00Z" */
    char status[24];          /* raw status, e.g. "Final", "Scheduled" */
    char status_state[16];    /* scheduled | in_progress | final | postponed | canceled | ... */
    char time[24];            /* live clock/status text, e.g. "Final", "1:00" */
    int  period;              /* current/last period, -1 if absent */
    bool postseason;          /* (unused since the ESPN migration) */
    bool postponed;
    int  home_score;          /* -1 when not available (game not played) */
    int  visitor_score;       /* -1 when not available */
    char venue[48];           /* arena + ", City ST" where the tail fits whole - ESPN names every event's venue, all leagues */
    char home_abbr[8];
    char home_name[48];       /* full name, e.g. "Dallas Mavericks" */
    char visitor_abbr[8];
    char visitor_name[48];
    int  home_id;             /* team id in the feed's namespace (see league_t) */
    int  visitor_id;
    league_t league;          /* picks the logo table + period conventions */
    int  season;              /* (unused since the ESPN migration) */
    int  home_q[7];           /* period-by-period points (NCAAB: H1,H2,OT..); -1 when the period has not been played */
    int  visitor_q[7];
    int  home_timeouts;       /* timeouts remaining, -1 when absent (ESPN carries none: the detail row hides) */
    int  visitor_timeouts;
    bool home_bonus;          /* (unused since the ESPN migration) */
    bool visitor_bonus;
} nba_game_t;
