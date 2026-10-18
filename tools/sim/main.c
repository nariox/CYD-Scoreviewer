/* Offline preview of the CYD cards screen: renders the real cards_screen.c
   against a mock game list and dumps a PNG (320x240) for review.

   build+run:  tools/sim/run.sh
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "cards_screen.h"
#include "espn_api.h"
#include "game_detail_screen.h"
#include "nba_api.h"
#include "nba_service.h"
#include "ui_common.h"

#define SIM_W 320
#define SIM_H 240

static uint16_t framebuffer[SIM_W * SIM_H];

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

static void dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", SIM_W, SIM_H);
    for (int i = 0; i < SIM_W * SIM_H; i++) {
        uint16_t c = framebuffer[i];
        unsigned char rgb[3] = {
            (unsigned char)(((c >> 11) & 0x1F) * 255 / 31),
            (unsigned char)(((c >> 5) & 0x3F) * 255 / 63),
            (unsigned char)((c & 0x1F) * 255 / 31)
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("wrote %s\n", path);
}

/* Scenarios: which mock games to show, and the wifi badge state.
   Run one per process (lv_init is once-per-process) and dump each PNG.
   Game order: 0-3 finals, 4-5 live, 6-8 scheduled/postponed, 9-11 more. */
static void select_scenario(const char *scenario, const nba_game_t **games,
                            int *count, wifi_state_t *wifi, const char **message)
{
    static const nba_game_t all[] = {
#define G(d, st, stat, tm, va, vi, vn, vs, ha, hi, hn, hs) \
        { .date = d, .status = stat, .status_state = st, .time = tm, \
          .visitor_abbr = va, .visitor_id = vi, .visitor_name = vn, \
          .home_abbr = ha, .home_id = hi, .home_name = hn, \
          .visitor_score = vs, .home_score = hs }
        G("2026-10-14", "final", "Final", "Final", "BKN", 3, "Brooklyn Nets", 128,
          "MIA", 16, "Miami Heat", 144),
        G("2026-10-14", "final", "Final", "Final", "CHA", 4, "Charlotte Hornets", 93,
          "BOS", 2, "Boston Celtics", 114),
        G("2026-10-15", "final", "Final", "Final", "DET", 9, "Detroit Pistons", 115,
          "NYK", 20, "New York Knicks", 106),
        G("2026-10-15", "final", "Final", "Final", "LAL", 14, "Los Angeles Lakers", 99,
          "GSW", 10, "Golden State Warriors", 99),
        G("2026-10-16", "in_progress", "Q2 05:31", "Q2 05:31", "DAL", 7,
          "Dallas Mavericks", 44, "HOU", 11, "Houston Rockets", 39),
        G("2026-10-16", "in_progress", "Halftime", "Halftime", "PHI", 23,
          "Philadelphia 76ers", 71, "TOR", 28, "Toronto Raptors", 68),
        G("2026-10-17", "scheduled", "Scheduled", "7:00", "SAS", 27,
          "San Antonio Spurs", -1, "IND", 12, "Indiana Pacers", -1),
        G("2026-10-17", "scheduled", "Scheduled", "10:00", "SAC", 26,
          "Sacramento Kings", -1, "POR", 25, "Portland Trail Blazers", -1),
        G("2026-10-17", "postponed", "Postponed", "", "CLE", 6,
          "Cleveland Cavaliers", -1, "ATL", 1, "Atlanta Hawks", -1),
        G("2026-10-18", "scheduled", "Scheduled", "19:30", "MIL", 17,
          "Milwaukee Bucks", -1, "CHI", 5, "Chicago Bulls", -1),
        G("2026-10-18", "final", "Final", "Final", "ORL", 22, "Orlando Magic", 88,
          "MEM", 15, "Memphis Grizzlies", 91),
        G("2026-10-18", "final", "Final", "Final", "MIN", 18,
          "Minnesota Timberwolves", 120, "DEN", 8, "Denver Nuggets", 118),
    };
    /* Board scenarios render the default page (8): 12 mock cards + the
       league-tab header overflow the 64 KB LVGL pool (the tabs added ~5
       points on top of the old 92%, so PAGE_SIZE=12 is out of reach). */
    const int board = 8;

    /* ESPN college slate mock: ids are the verified ESPN school ids from
       fixtures/espn_*.json / tools/logos/teams_ncaa.json - the same keys the
       baked ncaa logo table uses, so the logos must render on these cards. */
    static const nba_game_t ncaa[] = {
#define NG(d, st, stat, tm, va, vi, vn, vs, ha, hi, hn, hs) \
        { .league = LEAGUE_NCAAB, .date = d, .status = stat, .status_state = st, \
          .time = tm, .visitor_abbr = va, .visitor_id = vi, .visitor_name = vn, \
          .home_abbr = ha, .home_id = hi, .home_name = hn, \
          .visitor_score = vs, .home_score = hs }
        NG("2026-11-10", "scheduled", "Scheduled", "11/10 - 6:00 PM EST",
           "FSU", 52, "Florida State Seminoles", -1, "FLA", 57, "Florida Gators", -1),
        /* Wichita State (Missouri Valley) is deliberately outside the baked
           table now - the card must draw the placeholder ring, not vanish. */
        NG("2026-01-24", "canceled", "Canceled", "Canceled", "WCU", 271,
           "Wichita State Shockers", -1, "ILL", 356,
           "Illinois Fighting Illini", -1),
        NG("2026-11-10", "in_progress", "2nd 8:59", "08:59", "GT", 59,
           "Georgia Tech Yellow Jackets", 33, "MSU", 127,
           "Michigan State Spartans", 28),
        NG("2026-01-24", "final", "Final/OT", "Final/OT", "VILL", 222,
           "Villanova Wildcats", 67, "CONN", 41, "UConn Huskies", 75),
        NG("2026-01-24", "final", "Final", "Final", "ARIZ", 12,
           "Arizona Wildcats", 66, "WVU", 277, "West Virginia Mountaineers", 62),
        NG("2026-01-24", "final", "Final", "Final", "PUR", 2509,
           "Purdue Boilermakers", 77, "ILL", 356, "Illinois Fighting Illini", 71),
        NG("2026-01-24", "final", "Final", "Final", "DUKE", 150,
           "Duke Blue Devils", 81, "WAKE", 154, "Wake Forest Demon Deacons", 69),
        NG("2026-01-24", "final", "Final", "Final", "MSST", 344,
           "Mississippi State Bulldogs", 71, "VAN", 238,
           "Vanderbilt Commodores", 66),
        NG("2026-01-24", "final", "Final", "Final", "UVA", 258,
           "Virginia Cavaliers", 63, "UNC", 153, "North Carolina Tar Heels", 68),
    };
    const int ncaa_total = (int)(sizeof(ncaa) / sizeof(ncaa[0]));

    /* WNBA board (balldontlie /wnba shape: period stats absent entirely,
       ids from the baked WNBA table: LV=8 NY=1 MIN=7 PHX=10 DAL=11 GS=13). */
    static const nba_game_t wnba[] = {
        { .league = LEAGUE_WNBA, .date = "2026-10-14",
          .datetime = "2026-10-15T01:30:00.000Z",
          .status = "final", .status_state = "final", .time = "Final",
          .period = 4, .postseason = true,
          .visitor_abbr = "LV", .visitor_id = 8, .visitor_name = "Las Vegas Aces",
          .visitor_score = 89,
          .home_abbr = "NY", .home_id = 1, .home_name = "New York Liberty",
          .home_score = 84, .visitor_q = {-1,-1,-1,-1,-1,-1,-1},
          .home_q = {-1,-1,-1,-1,-1,-1,-1},
          .visitor_timeouts = -1, .home_timeouts = -1 },
        { .league = LEAGUE_WNBA, .date = "2026-10-17",
          .datetime = "2026-10-17T23:30:00.000Z",
          .status = "Q2 5:31", .status_state = "in_progress", .time = "Q2 5:31",
          .period = 2,
          .visitor_abbr = "MIN", .visitor_id = 7, .visitor_name = "Minnesota Lynx",
          .visitor_score = 41, .visitor_q = {-1,-1,-1,-1,-1,-1,-1},
          .home_abbr = "PHX", .home_id = 10, .home_name = "Phoenix Mercury",
          .home_score = 38, .home_q = {-1,-1,-1,-1,-1,-1,-1},
          .visitor_timeouts = -1, .home_timeouts = -1 },
        { .league = LEAGUE_WNBA, .date = "2026-10-20",
          .datetime = "2026-10-21T00:00:00.000Z",
          .status = "scheduled", .status_state = "scheduled",
          .time = "10/20 - 8:00 PM",
          .visitor_abbr = "DAL", .visitor_id = 11, .visitor_name = "Dallas Wings",
          .visitor_score = -1, .visitor_q = {-1,-1,-1,-1,-1,-1,-1},
          .home_abbr = "GS", .home_id = 13, .home_name = "Golden State Valkyries",
          .home_score = -1, .home_q = {-1,-1,-1,-1,-1,-1,-1},
          .visitor_timeouts = -1, .home_timeouts = -1 },
    };
    const int wnba_total = (int)(sizeof(wnba) / sizeof(wnba[0]));

    *wifi = WIFI_STATE_CONNECTED;
    *message = NULL;
    if (strcmp(scenario, "paged") == 0 || strcmp(scenario, "oldest") == 0) {
        *games = all;            /* history page: the early-slate eight */
        *count = board;
    } else {
        *games = &all[4];        /* "today" page: live/upcoming/postponed/final */
        *count = board;
    }

    if (strcmp(scenario, "live") == 0) {
        *games = &all[4];       /* live + upcoming board */
        *count = 6;
    } else if (strcmp(scenario, "ncaa") == 0) {
        *games = ncaa;          /* ESPN college slate, baked school logos */
        *count = ncaa_total;
    } else if (strcmp(scenario, "wnba") == 0) {
        *games = wnba;          /* balldontlie WNBA playoff board (no q stats) */
        *count = wnba_total;
    } else if (strcmp(scenario, "offline") == 0) {
        *count = 0;
        *wifi = WIFI_STATE_DISCONNECTED;
        *message = "No data - check Wi-Fi";
    } else if (strcmp(scenario, "connecting") == 0) {
        *count = 0;
        *wifi = WIFI_STATE_CONNECTING;   /* greyed blinking fan in the badge */
        *message = "No data - check Wi-Fi";
    } else if (strcmp(scenario, "empty") == 0) {
        *count = 0;
        *message = "No games found";
    }
}

/* Detail-view mocks: the period-by-period fields only matter once a card is
   tapped, so they live here instead of cluttering the board table above. */
static const nba_game_t DETAIL_FINAL = {
    .id = 21717914, .date = "2026-10-16", .datetime = "2026-10-17T00:00:00.000Z",
    .status = "Final", .status_state = "final", .time = "Final", .period = 4,
    .visitor_abbr = "BKN", .visitor_id = 3, .visitor_name = "Brooklyn Nets", .visitor_score = 128,
    .visitor_q = {39, 34, 29, 33, -1, -1, -1}, .visitor_timeouts = 2,
    .home_abbr = "MIA", .home_id = 16, .home_name = "Miami Heat", .home_score = 144,
    .home_q = {34, 20, 24, 33, -1, -1, -1}, .home_timeouts = 1,
};

static const nba_game_t DETAIL_LIVE = {
    .id = 21718002, .date = "2026-10-16", .datetime = "2026-10-16T23:30:00.000Z",
    .status = "Q2 05:31", .status_state = "in_progress", .time = "05:31",
    .period = 2,
    .visitor_abbr = "DAL", .visitor_id = 7, .visitor_name = "Dallas Mavericks", .visitor_score = 44,
    .visitor_q = {25, 19, -1, -1, -1, -1, -1}, .visitor_timeouts = 2,
    .home_abbr = "HOU", .home_id = 11, .home_name = "Houston Rockets", .home_score = 39,
    .home_q = {20, 19, -1, -1, -1, -1, -1}, .home_timeouts = 4,
    .home_bonus = true,
};

static const nba_game_t DETAIL_OT = {
    .id = 21718077, .date = "2026-10-18", .datetime = "2026-10-19T01:00:00.000Z",
    .status = "Final/OT", .status_state = "final", .time = "Final", .period = 5,
    .visitor_abbr = "MIN", .visitor_id = 18, .visitor_name = "Minnesota Timberwolves",
    .visitor_score = 120,
    .visitor_q = {28, 25, 30, 25, 12, -1, -1}, .visitor_timeouts = 0,
    .home_abbr = "DEN", .home_id = 8, .home_name = "Denver Nuggets", .home_score = 118,
    .home_q = {30, 26, 27, 23, 12, -1, -1}, .home_timeouts = 0,
    .postseason = true,
};

static const nba_game_t DETAIL_SCHEDULED = {
    .id = 21718100, .date = "2026-10-17", .datetime = "2026-10-18T00:00:00.000Z",
    .status = "Scheduled", .status_state = "scheduled", .time = "7:00",
    .period = -1,
    .visitor_abbr = "SAS", .visitor_id = 27, .visitor_name = "San Antonio Spurs", .visitor_score = -1,
    .visitor_q = {-1, -1, -1, -1, -1, -1, -1}, .visitor_timeouts = -1,
    .home_abbr = "IND", .home_id = 12, .home_name = "Indiana Pacers", .home_score = -1,
    .home_q = {-1, -1, -1, -1, -1, -1, -1}, .home_timeouts = -1,
};

/* ESPN college detail mocks (numbers straight from the Jan 24 fixture):
   NCAAB plays halves -> the table must show H1/H2/OT, NCAAW keeps quarters. */
static const nba_game_t DETAIL_MBB_OT = {
    .id = 401822913, .date = "2026-01-24", .datetime = "2026-01-24T17:30Z",
    .league = LEAGUE_NCAAB,
    .status = "Final/OT", .status_state = "final", .time = "", .period = 3,
    .visitor_abbr = "VILL", .visitor_id = 222, .visitor_name = "Villanova Wildcats",
    .visitor_score = 67,
    .visitor_q = {31, 30, 6, -1, -1, -1, -1}, .visitor_timeouts = -1,
    .home_abbr = "CONN", .home_id = 41, .home_name = "UConn Huskies", .home_score = 75,
    .home_q = {27, 34, 14, -1, -1, -1, -1}, .home_timeouts = -1,
};

static const nba_game_t DETAIL_WBB = {
    .id = 401807579, .date = "2026-01-24", .datetime = "2026-01-24T18:00Z",
    .league = LEAGUE_NCAAW,
    .status = "Final", .status_state = "final", .time = "", .period = 4,
    .visitor_abbr = "UGA", .visitor_id = 61, .visitor_name = "Georgia Bulldogs",
    .visitor_score = 72,
    .visitor_q = {21, 13, 18, 20, -1, -1, -1}, .visitor_timeouts = -1,
    .home_abbr = "UK", .home_id = 96, .home_name = "Kentucky Wildcats", .home_score = 67,
    .home_q = {12, 14, 24, 17, -1, -1, -1}, .home_timeouts = -1,
};

/* balldontlie's WNBA payload has no period stats at all - the box-score
   table must hide entirely instead of showing a wall of dashes. */
static const nba_game_t DETAIL_WNBA = {
    .id = 1392687, .date = "2026-10-07", .datetime = "2026-10-07T23:30:00.000Z",
    .league = LEAGUE_WNBA,
    .status = "final", .status_state = "final", .time = "Final", .period = 5,
    .postseason = true,
    .visitor_abbr = "LV", .visitor_id = 8, .visitor_name = "Las Vegas Aces",
    .visitor_score = 98,
    .home_abbr = "NY", .home_id = 1, .home_name = "New York Liberty",
    .home_score = 101,
    /* balldontlie /wnba has no per-period stats: the parser leaves -1; an
       omitted array in a struct literal would zero-init and fake a table. */
    .visitor_q = {-1, -1, -1, -1, -1, -1, -1},
    .home_q = {-1, -1, -1, -1, -1, -1, -1},
    .visitor_timeouts = -1, .home_timeouts = -1,
};

static const nba_game_t *detail_scenario(const char *scenario)
{
    if (strcmp(scenario, "detail") == 0)          return &DETAIL_FINAL;
    if (strcmp(scenario, "detail_live") == 0)     return &DETAIL_LIVE;
    if (strcmp(scenario, "detail_ot") == 0)       return &DETAIL_OT;
    if (strcmp(scenario, "detail_sched") == 0)    return &DETAIL_SCHEDULED;
    if (strcmp(scenario, "detail_mbb_ot") == 0)   return &DETAIL_MBB_OT;
    if (strcmp(scenario, "detail_wbb") == 0)      return &DETAIL_WBB;
    if (strcmp(scenario, "detail_wnba") == 0)     return &DETAIL_WNBA;
    return NULL;
}

/* ---- streaming-parser regression test (tools/sim/fixtures) --------------
   Real scoreboard bodies (padded to ~20 KB/event) pushed through the exact
   scanner the device streams over HTTPS. Exercises: halves vs quarters, an
   OT linescore set, string-vs-number scores, the newest-first cap on a 69
   event November day, and the scheduled/"TBD" mapping. */

static char *read_file(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "missing fixture %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (buf && fread(buf, 1, (size_t)len, f) == (size_t)len) {
        buf[len] = '\0';
        *len_out = (size_t)len;
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static void print_games(const nba_game_t *g, int n)
{
    for (int i = 0; i < n; i++) {
        printf("  %d %s @ %s %3d - %3d  %s st=%-11s pd=%2d t='%s' q=%d %d %d %d %d\n",
               g[i].id, g[i].visitor_abbr, g[i].home_abbr,
               g[i].visitor_score, g[i].home_score, g[i].date, g[i].status_state,
               g[i].period, g[i].time,
               g[i].visitor_q[0], g[i].visitor_q[1], g[i].visitor_q[2],
               g[i].visitor_q[3], g[i].visitor_q[4]);
    }
}

static int run_parse_tests(void)
{
    struct {
        const char *file;
        league_t league;
    } cases[] = {
        { "fixtures/espn_mbb_final.json", LEAGUE_NCAAB },
        { "fixtures/espn_wbb_final.json", LEAGUE_NCAAW },
        { "fixtures/espn_mbb_sched.json", LEAGUE_NCAAB },
        { "fixtures/espn_wnba_final.json", LEAGUE_NCAAW },
    };
    int fails = 0;

    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        size_t len = 0;
        char *body = read_file(cases[c].file, &len);
        if (!body) {
            return 1;
        }
        static nba_game_t games[ESPN_API_MAX_GAMES];
        int count = 0;
        espn_parse_scoreboard(body, len, cases[c].league, games,
                              ESPN_API_MAX_GAMES, &count);
        printf("%s: parsed %d game(s)\n", cases[c].file, count);
        print_games(games, count < 6 ? count : 6);

        if (c == 0) {   /* 19 finals, OT game parsed as halves + OT line */
            int vill = -1;
            for (int i = 0; i < count; i++) {
                if (games[i].id == 401822913) vill = i;
            }
            if (count != 19) { printf("FAIL: mbb count %d != 19\n", count); fails++; }
            if (vill < 0 || strcmp(games[vill].status, "Final/OT") != 0
                || games[vill].visitor_q[2] != 6 || games[vill].home_q[2] != 14
                || games[vill].visitor_score != 67 || games[vill].home_score != 75
                || games[vill].visitor_id != 222 || games[vill].home_id != 41) {
                printf("FAIL: CONN/VILL OT game\n"); fails++;
            }
        }
        if (c == 1) {   /* 9 WBB games, four linescores, winner is the away side */
            int uk = -1;
            for (int i = 0; i < count; i++) {
                if (games[i].id == 401807579) uk = i;
            }
            if (count != 9) { printf("FAIL: wbb count %d != 9\n", count); fails++; }
            if (uk < 0 || games[uk].visitor_id != 61 || games[uk].home_id != 96
                || games[uk].visitor_q[3] != 20 || games[uk].home_q[3] != 17
                || games[uk].visitor_score != 72) {
                printf("FAIL: UK/UGA wbb game\n"); fails++;
            }
        }
        if (c == 2) {   /* 69 scheduled events: keep the NEWEST cap-worth, sorted */
            if (count != ESPN_API_MAX_GAMES) {
                printf("FAIL: sched count %d != cap %d\n", count, ESPN_API_MAX_GAMES);
                fails++;
            }
            bool sorted = true, no_midnight = true;
            for (int i = 0; i < count; i++) {
                if (strcmp(games[i].status_state, "scheduled") != 0) {
                    printf("FAIL: sched state %s\n", games[i].status_state);
                    fails++;
                }
                if (i && strcmp(games[i].datetime, games[i - 1].datetime) > 0) {
                    sorted = false;
                }
                if (strstr(games[i].datetime, "T05:00Z")) no_midnight = false;
            }
            if (!sorted) { printf("FAIL: sched output not newest-first\n"); fails++; }
            if (!no_midnight) {
                printf("FAIL: kept earliest tip-offs instead of newest\n");
                fails++;
            }
        }
        if (c == 3) {   /* WNBA scoreboard: WBB league tag, finals parsed */
            if (count < 1) { printf("FAIL: wnba count %d\n", count); fails++; }
            for (int i = 0; i < count; i++) {
                if (games[i].league != LEAGUE_NCAAW ||
                    strcmp(games[i].status_state, "final") != 0) {
                    printf("FAIL: wnba game %d tag\n", i); fails++; break;
                }
            }
        }
        free(body);
    }
    printf("%s: espn parser tests\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *scenario = (argc > 1) ? argv[1] : "games";
    const char *out_path = (argc > 2) ? argv[2] : "/tmp/opencode/cards_screen.ppm";

    if (strcmp(scenario, "espnparse") == 0) {
        return run_parse_tests();     /* no display needed for the parser test */
    }

    const nba_game_t *games = NULL;
    int count = 0;
    wifi_state_t wifi;
    const char *message = NULL;
    select_scenario(scenario, &games, &count, &wifi, &message);
    if (strcmp(scenario, "ncaa") == 0) {
        nba_service_set_league(LEAGUE_NCAAB);   /* light the MBB tab (before create) */
    } else if (strcmp(scenario, "wnba") == 0) {
        nba_service_set_league(LEAGUE_WNBA);    /* light the WNBA tab */
    }

    lv_init();

    lv_display_t *disp = lv_display_create(SIM_W, SIM_H);
    lv_display_set_buffers(disp, framebuffer, NULL, sizeof(framebuffer),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_obj_t *scr = cards_screen_create();
    cards_screen_update_wifi_status(wifi);

    cards_page_t page = { .iso_newest = "2026-10-17", .iso_oldest = "2026-10-14",
                          .page = 1, .has_earlier = true, .has_later = false };
    if (strcmp(scenario, "paged") == 0) {          /* mid-history page */
        page.iso_newest = "2026-10-15";
        page.iso_oldest = "2026-10-14";
        page.page = 3;
        page.has_later = true;
    } else if (strcmp(scenario, "oldest") == 0) {  /* end of the history */
        page.iso_newest = "2026-10-14";
        page.iso_oldest = "2026-10-14";
        page.page = 6;
        page.has_earlier = false;
        page.has_later = true;
    } else if (strcmp(scenario, "ncaa") == 0) {    /* college slate dates */
        page.iso_newest = "2026-11-10";
        page.iso_oldest = "2026-01-24";
    } else if (message) {                          /* nothing to date-stamp */
        page.iso_newest = NULL;
        page.iso_oldest = NULL;
        page.has_later = false;
    }

    /* Tap-through scenarios first need the board's boot state. */
    const nba_game_t *detail = detail_scenario(scenario);

    if (message) {
        cards_screen_show_message(message, &page);
    } else if (detail) {
        /* Detail scenarios keep the board at its placeholder page: on the device
           the card page is dropped when a game opens, so this is the pool
           situation that matters (boot screens + the detail view). */
    } else {
        cards_screen_show_games(games, count, &page);
    }
    lv_screen_load(scr);

    if (detail) {
        game_detail_screen_set_back_scr(scr);
        game_detail_screen_show(detail);
    }

    for (int i = 0; i < 11; i++) {   /* 1.1s: wifi blink timer lands ON */
        lv_tick_inc(100);
        lv_timer_handler();
    }
    lv_refr_now(disp);

    if (detail && getenv("SIM_TREE")) {
        lv_obj_t *act = lv_screen_active();
        for (uint32_t i = 0; i < lv_obj_get_child_count(act); i++) {
            lv_obj_t *row = lv_obj_get_child(act, i);
            printf("row %u: x=%d y=%d w=%d kids=%u\n", i,
                   (int)lv_obj_get_x(row), (int)lv_obj_get_y(row),
                   (int)lv_obj_get_width(row), (unsigned)lv_obj_get_child_count(row));
            for (uint32_t j = 0; j < lv_obj_get_child_count(row); j++) {
                lv_obj_t *cell = lv_obj_get_child(row, j);
                printf("  grp %u: x=%d w=%d kids=%u\n", j,
                       (int)lv_obj_get_x(cell), (int)lv_obj_get_width(cell),
                       (unsigned)lv_obj_get_child_count(cell));
                for (uint32_t k = 0; k < lv_obj_get_child_count(cell); k++) {
                    lv_obj_t *c = lv_obj_get_child(cell, k);
                    printf("     cell %u: x=%d w=%d hidden=%d text=\"%s\"\n", k,
                           (int)lv_obj_get_x(c), (int)lv_obj_get_width(c),
                           (int)lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN),
                           lv_obj_check_type(c, &lv_label_class)
                               ? lv_label_get_text(c) : "<not a label>");
                }
            }
        }
    }

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("lv pool: %u of %u bytes (%u%% used, %u%% frag)\n",
           (unsigned)(mon.total_size - mon.free_size), (unsigned)mon.total_size,
           mon.used_pct, mon.frag_pct);

    dump_ppm(out_path);
    printf("scenario: %s\n", scenario);
    return 0;
}
