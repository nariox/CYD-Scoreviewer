/* Offline preview of the CYD cards screen: renders the real cards_screen.c
   against a mock game list and dumps a PNG (320x240) for review.

   build+run:  tools/sim/run.sh
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "cards_screen.h"
#include "game_detail_screen.h"
#include "nba_api.h"
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
#define G(d, st, stat, tm, va, vs, ha, hs) \
        { .date = d, .status = stat, .status_state = st, .time = tm, \
          .visitor_abbr = va, .home_abbr = ha, .visitor_score = vs, .home_score = hs }
        G("2026-10-14", "final", "Final", "Final", "BKN", 128, "MIA", 144),
        G("2026-10-14", "final", "Final", "Final", "CHA", 93, "BOS", 114),
        G("2026-10-15", "final", "Final", "Final", "DET", 115, "NYK", 106),
        G("2026-10-15", "final", "Final", "Final", "LAL", 99, "GSW", 99),
        G("2026-10-16", "in_progress", "Q2 05:31", "Q2 05:31", "DAL", 44, "HOU", 39),
        G("2026-10-16", "in_progress", "Halftime", "Halftime", "PHI", 71, "TOR", 68),
        G("2026-10-17", "scheduled", "Scheduled", "7:00", "SAS", -1, "IND", -1),
        G("2026-10-17", "scheduled", "Scheduled", "10:00", "SAC", -1, "POR", -1),
        G("2026-10-17", "postponed", "Postponed", "", "CLE", -1, "ATL", -1),
        G("2026-10-18", "scheduled", "Scheduled", "19:30", "MIL", -1, "CHI", -1),
        G("2026-10-18", "final", "Final", "Final", "ORL", 88, "MEM", 91),
        G("2026-10-18", "final", "Final", "Final", "MIN", 120, "DEN", 118),
    };
    const int total = (int)(sizeof(all) / sizeof(all[0]));

    *wifi = WIFI_STATE_CONNECTED;
    *message = NULL;
    *games = all;
    *count = total;

    if (strcmp(scenario, "live") == 0) {
        *games = &all[4];       /* live + upcoming board */
        *count = 6;
    } else if (strcmp(scenario, "offline") == 0) {
        *count = 0;
        *wifi = WIFI_STATE_DISCONNECTED;
        *message = "No data — check Wi-Fi";
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
    .visitor_abbr = "BKN", .visitor_name = "Brooklyn Nets", .visitor_score = 128,
    .visitor_q = {39, 34, 29, 33, -1, -1, -1}, .visitor_timeouts = 2,
    .home_abbr = "MIA", .home_name = "Miami Heat", .home_score = 144,
    .home_q = {34, 20, 24, 33, -1, -1, -1}, .home_timeouts = 1,
};

static const nba_game_t DETAIL_LIVE = {
    .id = 21718002, .date = "2026-10-16", .datetime = "2026-10-16T23:30:00.000Z",
    .status = "Q2 05:31", .status_state = "in_progress", .time = "05:31",
    .period = 2,
    .visitor_abbr = "DAL", .visitor_name = "Dallas Mavericks", .visitor_score = 44,
    .visitor_q = {25, 19, -1, -1, -1, -1, -1}, .visitor_timeouts = 2,
    .home_abbr = "HOU", .home_name = "Houston Rockets", .home_score = 39,
    .home_q = {20, 19, -1, -1, -1, -1, -1}, .home_timeouts = 4,
    .home_bonus = true,
};

static const nba_game_t DETAIL_OT = {
    .id = 21718077, .date = "2026-10-18", .datetime = "2026-10-19T01:00:00.000Z",
    .status = "Final/OT", .status_state = "final", .time = "Final", .period = 5,
    .visitor_abbr = "MIN", .visitor_name = "Minnesota Timberwolves",
    .visitor_score = 120,
    .visitor_q = {28, 25, 30, 25, 12, -1, -1}, .visitor_timeouts = 0,
    .home_abbr = "DEN", .home_name = "Denver Nuggets", .home_score = 118,
    .home_q = {30, 26, 27, 23, 12, -1, -1}, .home_timeouts = 0,
    .postseason = true,
};

static const nba_game_t DETAIL_SCHEDULED = {
    .id = 21718100, .date = "2026-10-17", .datetime = "2026-10-18T00:00:00.000Z",
    .status = "Scheduled", .status_state = "scheduled", .time = "7:00",
    .period = -1,
    .visitor_abbr = "SAS", .visitor_name = "San Antonio Spurs", .visitor_score = -1,
    .visitor_q = {-1, -1, -1, -1, -1, -1, -1}, .visitor_timeouts = -1,
    .home_abbr = "IND", .home_name = "Indiana Pacers", .home_score = -1,
    .home_q = {-1, -1, -1, -1, -1, -1, -1}, .home_timeouts = -1,
};

static const nba_game_t *detail_scenario(const char *scenario)
{
    if (strcmp(scenario, "detail") == 0)          return &DETAIL_FINAL;
    if (strcmp(scenario, "detail_live") == 0)     return &DETAIL_LIVE;
    if (strcmp(scenario, "detail_ot") == 0)       return &DETAIL_OT;
    if (strcmp(scenario, "detail_sched") == 0)    return &DETAIL_SCHEDULED;
    return NULL;
}

int main(int argc, char **argv)
{
    const char *scenario = (argc > 1) ? argv[1] : "games";
    const char *out_path = (argc > 2) ? argv[2] : "/tmp/opencode/cards_screen.ppm";

    const nba_game_t *games = NULL;
    int count = 0;
    wifi_state_t wifi;
    const char *message = NULL;
    select_scenario(scenario, &games, &count, &wifi, &message);

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

    for (int i = 0; i < 5; i++) {
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
