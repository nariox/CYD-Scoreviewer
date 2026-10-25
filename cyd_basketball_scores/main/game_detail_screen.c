#include "game_detail_screen.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "lvgl.h"
#include "league_logos.h"
#include "team_venues.h"
#include "ui_common.h"

static const char *TAG = "game_detail";

/* Box-score table: [row][column].
   Rows: 0 = period header, 1 = away team, 2 = home team.
   Columns: 0 = team name, 1..4 = periods (Q1..Q4, or H1/H2 for NCAAB - the
   unused columns are hidden for two-period games), 5 = overtime (hidden
   unless used), 6 = total. */
#define DET_ROWS  3
#define DET_COLS  7

static lv_obj_t *s_scr;
static lv_obj_t *s_back_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_status;
static lv_obj_t *s_score;
static lv_obj_t *s_score_row;
static lv_obj_t *s_logo_away;
static lv_obj_t *s_logo_home;
static lv_obj_t *s_cell[DET_ROWS][DET_COLS];
static lv_obj_t *s_row[DET_ROWS];
static lv_obj_t *s_note_date;
static lv_obj_t *s_note_venue;
static lv_obj_t *s_note_live;

/* Snapshot of the tapped game: the score feed keeps reordering its cache, so
   the screen must not hold a pointer into it. */
static nba_game_t s_game;

/* Throw the detail view away again. Keeping it alive would leave a page of
   cards (~15 KB) and the detail view (~12 KB) in the LVGL pool at the same time
   for the rest of the run, so the steady state after the first tap would sit
   ~13 points higher. lv_obj_delete_async() is the safe way to free a screen
   from a button event that lives on that very screen. */
static void teardown(void)
{
    if (!s_scr) {
        return;
    }
    lv_obj_delete_async(s_scr);
    s_scr = NULL;
    s_title = s_status = s_score = NULL;
    s_score_row = s_logo_away = s_logo_home = NULL;
    s_note_date = s_note_venue = s_note_live = NULL;
    for (int row = 0; row < DET_ROWS; row++) {
        s_row[row] = NULL;
        for (int col = 0; col < DET_COLS; col++) {
            s_cell[row][col] = NULL;
        }
    }
}

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_back_scr) {
        lv_scr_load(s_back_scr);
        teardown();
    }
}

static lv_obj_t *make_cell(lv_obj_t *parent, const lv_font_t *font,
                           uint32_t color, int width, int height,
                           lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "-");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    /* A fixed height is what makes LONG_DOT clip instead of wrap: with
       LV_SIZE_CONTENT the label would just grow a second line. */
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

/* One row of the box-score table. */
static void make_table_row(lv_obj_t *parent, int row, bool header_row)
{
    lv_obj_t *r = lv_obj_create(parent);
    s_row[row] = r;
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_SIZE_CONTENT, UI_DET_ROW_H);
    /* Theme padding on a short row container pushes its children outside it. */
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    const lv_font_t *num_font = header_row ? &lv_font_montserrat_12
                                           : &lv_font_montserrat_16;
    uint32_t num_col = header_row ? UI_COL_SUBTLE : UI_COL_TEXT;

    s_cell[row][0] = make_cell(r, &lv_font_montserrat_12,
                               header_row ? UI_COL_SUBTLE : UI_COL_TEXT,
                               UI_DET_NAME_W, UI_DET_NAME_H, LV_TEXT_ALIGN_LEFT);
    for (int c = 1; c <= 5; c++) {
        s_cell[row][c] = make_cell(r, num_font, num_col, UI_DET_COL_W,
                                   LV_SIZE_CONTENT, LV_TEXT_ALIGN_CENTER);
    }
    s_cell[row][6] = make_cell(r, num_font, num_col, UI_DET_TOTAL_W,
                               LV_SIZE_CONTENT, LV_TEXT_ALIGN_CENTER);

    if (header_row) {
        static const char *head[DET_COLS] = {"", "Q1", "Q2", "Q3", "Q4", "OT", "T"};
        for (int c = 0; c < DET_COLS; c++) {
            lv_label_set_text(s_cell[row][c], head[c]);
        }
    }
}

static void build(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_scrollable(s_scr, false);

    /* Header band, same shape as the cards screen: [back] away @ home [status] */
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_size(header, LV_PCT(100), UI_DET_HEAD_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COL_HEADER), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_scrollable(header, false);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *back = lv_btn_create(header);
    lv_obj_set_size(back, UI_BTN_MD, UI_BTN_MD);
    lv_obj_set_style_bg_color(back, lv_color_hex(UI_COL_HEADER), LV_PART_MAIN);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_margin_left(back, 6, 0);
    lv_obj_set_scrollable(back, false);
    lv_obj_add_event_cb(back, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_label, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);
    lv_obj_center(back_label);

    s_title = lv_label_create(header);
    lv_label_set_text(s_title, "-- @ --");
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_title, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_left(s_title, 8, 0);
    lv_obj_set_flex_grow(s_title, 1);

    s_status = lv_label_create(header);
    lv_label_set_text(s_status, "");
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_PLACEHOLDER), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_right(s_status, 10, 0);

    /* Headline score (or tip-off time for a game that has not started),
       flanked by the team logos - away left, home right, same side convention
       as the cards. The sources are set per game in fill(); a team without a
       baked logo just hides its slot. */
    s_score_row = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_score_row);
    lv_obj_set_size(s_score_row, LV_PCT(100), UI_DET_SCORE_H);
    lv_obj_set_style_pad_column(s_score_row, 12, 0);
    lv_obj_set_scrollable(s_score_row, false);
    lv_obj_set_flex_flow(s_score_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_score_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_score_row, LV_ALIGN_TOP_MID, 0, UI_DET_HEAD_H + 4);

    s_logo_away = lv_image_create(s_score_row);

    s_score = lv_label_create(s_score_row);
    lv_label_set_text(s_score, "-- - --");
    lv_obj_set_style_text_color(s_score, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_text_font(s_score, &lv_font_montserrat_20, 0);

    s_logo_home = lv_image_create(s_score_row);
    /* Hidden until fill() finds a logo for that team. */
    lv_obj_set_hidden(s_logo_away, true);
    lv_obj_set_hidden(s_logo_home, true);

    /* Box-score table. */
    lv_obj_t *table = lv_obj_create(s_scr);
    lv_obj_remove_style_all(table);
    lv_obj_set_size(table, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(table, LV_ALIGN_TOP_MID, 0, UI_DET_HEAD_H + UI_DET_SCORE_Y);
    lv_obj_set_style_pad_all(table, 0, 0);
    lv_obj_set_style_pad_row(table, 2, 0);
    lv_obj_set_scrollable(table, false);
    lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(table, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    for (int row = 0; row < DET_ROWS; row++) {
        make_table_row(table, row, row == 0);
    }

    /* Footer: game date + tip-off, the arena, and the live details (timeouts
       left). The venue row is created hidden: fill() stacks date/venue/live
       when an arena is known and collapses back to the two-line layout when
       not. */
    s_note_date = lv_label_create(s_scr);
    lv_label_set_text(s_note_date, "");
    lv_obj_set_style_text_color(s_note_date, lv_color_hex(UI_COL_PLACEHOLDER), 0);
    lv_obj_set_style_text_font(s_note_date, &lv_font_montserrat_12, 0);
    lv_obj_align(s_note_date, LV_ALIGN_BOTTOM_MID, 0, -30);

    s_note_venue = lv_label_create(s_scr);
    lv_label_set_text(s_note_venue, "");
    lv_obj_set_style_text_color(s_note_venue, lv_color_hex(UI_COL_PLACEHOLDER), 0);
    lv_obj_set_style_text_font(s_note_venue, &lv_font_montserrat_12, 0);
    lv_obj_align(s_note_venue, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_hidden(s_note_venue, true);

    s_note_live = lv_label_create(s_scr);
    lv_label_set_text(s_note_live, "");
    lv_obj_set_style_text_color(s_note_live, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_text_font(s_note_live, &lv_font_montserrat_12, 0);
    lv_obj_align(s_note_live, LV_ALIGN_BOTTOM_MID, 0, -12);
}

/* "2026-10-16" + "2026-10-17T00:00:00.000Z" -> "Oct 16, 2026 - tip-off 00:00 UTC".
   The API's `date` is the US game date while `datetime` is UTC, so both are
   labelled instead of quietly mixed. */
static void fmt_game_line(const nba_game_t *g, char *out, size_t n)
{
    static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int year = 0, month = 0, day = 0;
    bool have_date = (g->date[0] &&
                      sscanf(g->date, "%d-%d-%d", &year, &month, &day) == 3 &&
                      month >= 1 && month <= 12);

    int hour = -1, min = 0;
    const char *t = strchr(g->datetime, 'T');
    if (t && sscanf(t + 1, "%d:%d", &hour, &min) != 2) {
        hour = -1;
    }

    if (have_date && hour >= 0) {
        snprintf(out, n, "%s %d, %d - tip-off %02d:%02d UTC",
                 MONTHS[month - 1], day, year, hour, min);
    } else if (have_date) {
        snprintf(out, n, "%s %d, %d", MONTHS[month - 1], day, year);
    } else {
        snprintf(out, n, "%.11s", g->date[0] ? g->date : "--");
    }
}

/* Where the game is played. The ESPN feed carries the arena per game (exact
   even at neutral sites) for every league, so the baked pro home-arena
   table is now only a fallback for old NVS snapshots that predate it.
   NULL = unknown. */
static const char *venue_for(const nba_game_t *g)
{
    if (g->venue[0]) {
        return g->venue;
    }
    switch (g->league) {
    case LEAGUE_NBA:
        return nba_team_venue(g->home_id);
    case LEAGUE_WNBA:
        return wnba_team_venue(g->home_id);
    default:
        return NULL;   /* college without a parsed venue: nothing to show */
    }
}

static void set_cell(int row, int col, const char *text)
{
    if (s_cell[row][col]) {
        lv_label_set_text(s_cell[row][col], text);
    }
}

/* One box-score cell: "-" when the period has not been played. */
static void set_cell_num(int row, int col, int value)
{
    char cell[16];
    if (value >= 0) {
        snprintf(cell, sizeof(cell), "%d", value);
    } else {
        snprintf(cell, sizeof(cell), "-");
    }
    set_cell(row, col, cell);
}

static void fill(void)
{
    const nba_game_t *g = &s_game;
    char buf[128];
    bool live = (strcmp(g->status_state, "in_progress") == 0);
    bool have_score = (g->visitor_score >= 0 && g->home_score >= 0);

    /* NCAAB plays two halves, everyone else four quarters: that decides the
       table's column labels and where the OT column starts soaking periods. */
    const int n_reg = (g->league == LEAGUE_NCAAB) ? 2 : 4;
    const char *ptag = (g->league == LEAGUE_NCAAB) ? "H" : "Q";

    /* Field widths are spelled out (%.8s) so -Wformat-truncation can prove the
       output fits, and a surprising API string can never overrun the label. */
    snprintf(buf, sizeof(buf), "%.8s @ %.8s",
             g->visitor_abbr[0] ? g->visitor_abbr : "--",
             g->home_abbr[0] ? g->home_abbr : "--");
    lv_label_set_text(s_title, buf);

    if (live && g->period > 0) {
        snprintf(buf, sizeof(buf), "%s%d %.15s", ptag, g->period,
                 g->time[0] ? g->time : "");
    } else {
        snprintf(buf, sizeof(buf), "%.23s", g->status[0] ? g->status : "--");
    }
    lv_label_set_text(s_status, buf);
    lv_obj_set_style_text_color(s_status, lv_color_hex(live ? UI_COL_OK
                                                            : UI_COL_PLACEHOLDER), 0);

    if (have_score) {
        snprintf(buf, sizeof(buf), "%d - %d", g->visitor_score, g->home_score);
        lv_obj_set_style_text_color(s_score, lv_color_hex(live ? UI_COL_OK
                                                               : UI_COL_TEXT), 0);
    } else if (g->time[0]) {
        snprintf(buf, sizeof(buf), "%.23s", g->time);
        lv_obj_set_style_text_color(s_score, lv_color_hex(UI_COL_ACCENT), 0);
    } else {
        snprintf(buf, sizeof(buf), "%.23s", g->status[0] ? g->status : "--");
        lv_obj_set_style_text_color(s_score, lv_color_hex(UI_COL_PLACEHOLDER), 0);
    }
    lv_label_set_text(s_score, buf);

    /* Logos on the score line: hidden when that team has no baked bitmap.
       The table follows the feed's id namespace (game_logo). */
    const lv_image_dsc_t *la = game_logo(g->league, g->visitor_id);
    if (la) {
        lv_image_set_src(s_logo_away, la);
        lv_obj_set_hidden(s_logo_away, false);
    } else {
        lv_obj_set_hidden(s_logo_away, true);
    }
    const lv_image_dsc_t *lh = game_logo(g->league, g->home_id);
    if (lh) {
        lv_image_set_src(s_logo_home, lh);
        lv_obj_set_hidden(s_logo_home, false);
    } else {
        lv_obj_set_hidden(s_logo_home, true);
    }

    /* Winner bright, loser muted - same convention as the cards. */
    uint32_t away_col = UI_COL_TEXT, home_col = UI_COL_TEXT;
    if (have_score && g->visitor_score != g->home_score) {
        away_col = (g->visitor_score > g->home_score) ? UI_COL_TEXT : UI_COL_PLACEHOLDER;
        home_col = (g->home_score > g->visitor_score) ? UI_COL_TEXT : UI_COL_PLACEHOLDER;
    }
    lv_obj_set_style_text_color(s_cell[1][0], lv_color_hex(away_col), 0);
    lv_obj_set_style_text_color(s_cell[2][0], lv_color_hex(home_col), 0);
    set_cell(1, 0, g->visitor_name[0] ? g->visitor_name : g->visitor_abbr);
    set_cell(2, 0, g->home_name[0] ? g->home_name : g->home_abbr);

    /* Overtime is one column (the rare double-OT is folded into it); the column
       only shows up on games that actually went to OT. Regular periods are
       n_reg long, so for NCAAB everything from q[2] up is OT. */
    int ot[2] = {0, 0};
    bool has_ot = false;
    for (int i = n_reg; i < 7; i++) {
        if (g->visitor_q[i] > 0) { ot[0] += g->visitor_q[i]; has_ot = true; }
        if (g->home_q[i] > 0)     { ot[1] += g->home_q[i];     has_ot = true; }
    }

    for (int row = 1; row <= 2; row++) {
        const int *q = (row == 1) ? g->visitor_q : g->home_q;
        int played = 0, sum = 0;
        for (int c = 0; c < n_reg; c++) {
            if (q[c] >= 0) {
                sum += q[c];
                played++;
            }
            set_cell_num(row, 1 + c, q[c]);
        }
        set_cell_num(row, 5, has_ot ? ot[row - 1] : -1);
        if (has_ot) {
            sum += ot[row - 1];
        }

        int total = (row == 1) ? g->visitor_score : g->home_score;
        if (total < 0) {
            total = played ? sum : -1;
        }
        set_cell_num(row, 6, total);
    }

    /* Period column headers follow the league ("H1/H2" vs "Q1..Q4"), and the
       unused quarter columns disappear for the two-period game. */
    for (int c = 1; c <= 4; c++) {
        bool used = (c <= n_reg);
        if (used) {
            char head[8];
            snprintf(head, sizeof(head), "%s%d", ptag, c);
            set_cell(0, c, head);
        }
        for (int row = 0; row < DET_ROWS; row++) {
            if (used) {
                lv_obj_set_hidden(s_cell[row][c], false);
            } else {
                lv_obj_set_hidden(s_cell[row][c], true);
            }
        }
    }

    for (int row = 0; row < DET_ROWS; row++) {
        if (has_ot) {
            lv_obj_set_hidden(s_cell[row][5], false);
        } else {
            lv_obj_set_hidden(s_cell[row][5], true);
        }
    }

    /* A table of dashes says nothing - when no period scores came in (the
       ESPN scoreboard omits linescores for some unplayed/cleared events),
       the whole table hides and the score line carries the story. */
    bool any_stats = has_ot;
    for (int i = 0; i < n_reg && !any_stats; i++) {
        any_stats = (g->visitor_q[i] >= 0) || (g->home_q[i] >= 0);
    }
    for (int row = 0; row < DET_ROWS; row++) {
        if (any_stats) {
            lv_obj_set_hidden(s_row[row], false);
        } else {
            lv_obj_set_hidden(s_row[row], true);
        }
    }

    fmt_game_line(g, buf, sizeof(buf));
    lv_label_set_text(s_note_date, buf);

    /* Venue row between date and live line: the arena never fits on a 46 px
       card (see the '*' home marker on the board), so this is where the
       "where" lives. With no arena known the footer collapses back to the
       old two-line placement. */
    const char *venue = venue_for(g);
    if (venue) {
        lv_label_set_text(s_note_venue, venue);
        lv_obj_set_hidden(s_note_venue, false);
        lv_obj_align(s_note_date, LV_ALIGN_BOTTOM_MID, 0, -46);
        lv_obj_align(s_note_venue, LV_ALIGN_BOTTOM_MID, 0, -30);
    } else {
        lv_label_set_text(s_note_venue, "");
        lv_obj_set_hidden(s_note_venue, true);
        lv_obj_align(s_note_date, LV_ALIGN_BOTTOM_MID, 0, -30);
    }

    if (live && g->visitor_timeouts >= 0 && g->home_timeouts >= 0) {
        snprintf(buf, sizeof(buf), "Timeouts %.8s %d - %d %.8s",
                 g->visitor_abbr, g->visitor_timeouts,
                 g->home_timeouts, g->home_abbr);
        lv_label_set_text(s_note_live, buf);
        lv_obj_set_hidden(s_note_live, false);
    } else {
        lv_label_set_text(s_note_live, "");
        lv_obj_set_hidden(s_note_live, true);
    }
}

void game_detail_screen_show(const nba_game_t *game)
{
    if (!game) {
        return;
    }
    if (!s_scr) {
        build();
    }

    s_game = *game;
    fill();
    lv_scr_load(s_scr);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const char *venue = venue_for(&s_game);
    ESP_LOGI(TAG, "opened game %d @ %s: lv pool %u%% used (%u%% frag), ESP heap %u",
             s_game.id, venue ? venue : "(no venue)",
             mon.used_pct, mon.frag_pct, esp_get_free_heap_size());
}

void game_detail_screen_set_back_scr(lv_obj_t *scr)
{
    s_back_scr = scr;
}

void game_detail_screen_back(void)
{
    if (s_scr && lv_screen_active() == s_scr && s_back_scr) {
        lv_scr_load(s_back_scr);
        teardown();
    }
}
