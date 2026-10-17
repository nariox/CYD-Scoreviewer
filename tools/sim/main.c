/* Offline preview of the CYD cards screen: renders the real cards_screen.c
   against a mock game list and dumps a PNG (320x240) for review.

   build+run:  tools/sim/run.sh
*/
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "cards_screen.h"
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

    if (message) {
        cards_screen_show_message(message, &page);
    } else {
        cards_screen_show_games(games, count, &page);
    }
    lv_screen_load(scr);

    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
    lv_refr_now(disp);

    dump_ppm(out_path);
    printf("scenario: %s\n", scenario);
    return 0;
}
