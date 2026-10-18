#include "cards_screen.h"
#include "game_detail_screen.h"
#include "league_logos.h"
#include "nba_service.h"
#include "settings_screen.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "lvgl.h"
#include "ui_common.h"

static const char *TAG = "cards";

static lv_obj_t *settings_scr = NULL;
static lv_obj_t *wifi_status_label = NULL;
static lv_obj_t *cards_container = NULL;
static lv_obj_t *page_date_label = NULL;
static lv_obj_t *page_index_label = NULL;
static lv_obj_t *page_prev_btn = NULL;
static lv_obj_t *page_next_btn = NULL;

/* ---- league selector (the header's tabs replace the old static title) --- */
static lv_obj_t *league_tabs[4];
static const char *const TAB_LABELS[4] = {"NBA", "WNBA", "MBB", "WBB"};
static const league_t TAB_LEAGUES[4] = {LEAGUE_NBA, LEAGUE_WNBA,
                                        LEAGUE_NCAAB, LEAGUE_NCAAW};

/* Lit tab = active feed. The tabs are single labels, so colours go straight
   on them (the make_page_button trap - dimming via the parent - only bites
   when a child label owns its own style). */
static void style_league_tabs(void)
{
    league_t cur = nba_service_league();
    for (int i = 0; i < 4; i++) {
        bool on = (TAB_LEAGUES[i] == cur);
        lv_obj_set_style_bg_color(league_tabs[i],
                                  lv_color_hex(on ? UI_COL_PRESSED
                                                  : UI_COL_HEADER), 0);
        lv_obj_set_style_text_color(league_tabs[i],
                                    lv_color_hex(on ? UI_COL_TEXT
                                                    : UI_COL_SUBTLE), 0);
    }
}

/* LVGL context: the port mutex is already held here, so this callback must
   not take it - nba_service_set_league only sets flags and gives a
   semaphore, and the "Loading..." repaint happens on the worker task. */
static void league_tab_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    nba_service_set_league(TAB_LEAGUES[idx]);
    style_league_tabs();      /* instant feedback while the worker refetches */
}

static void settings_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (settings_scr) {
        lv_scr_load(settings_scr);
    }
}

static void refresh_btn_event_cb(lv_event_t *e)
{
    (void)e;
    nba_service_request_refresh();
}

/* Pager: older / newer page of games. The feed worker does the fetching and
   pushes the new page back to us. */
static void page_earlier_btn_cb(lv_event_t *e)
{
    (void)e;
    nba_service_step_page(1);
}

static void page_later_btn_cb(lv_event_t *e)
{
    (void)e;
    nba_service_step_page(-1);
}

void cards_screen_set_settings_scr(lv_obj_t *scr)
{
    settings_scr = scr;
}

void cards_screen_update_wifi_status(wifi_state_t state)
{
    if (!wifi_status_label) return;

    const char *text;
    lv_color_t color;

    switch (state) {
        case WIFI_STATE_CONNECTED:
            text = "Connected";
            color = lv_color_hex(UI_COL_OK);
            break;
        case WIFI_STATE_CONNECTING:
        case WIFI_STATE_SCANNING:
            text = "Connecting...";
            color = lv_color_hex(UI_COL_WARN);
            break;
        case WIFI_STATE_FAILED:
        case WIFI_STATE_DISCONNECTED:
            text = "Offline";   /* fits the badge width; "Disconnected" clips */
            color = lv_color_hex(UI_COL_DANGER);
            break;
        default:
            text = "";
            color = lv_color_hex(UI_COL_SUBTLE);
            break;
    }

    lv_label_set_text(wifi_status_label, text);
    lv_obj_set_style_text_color(wifi_status_label, color, 0);
}

/* "2026-10-14" -> "Oct 14" (balldontlie game dates are US-based). */
static void fmt_game_date(const char *iso, char *out, size_t n)
{
    static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int year = 0, month = 0, day = 0;

    if (iso && sscanf(iso, "%d-%d-%d", &year, &month, &day) == 3 &&
        month >= 1 && month <= 12 && day >= 1 && day <= 31) {
        snprintf(out, n, "%s %d", MONTHS[month - 1], day);
    } else {
        snprintf(out, n, "%s", iso && iso[0] ? iso : "--");
    }
}

/* Middle text of a card: the score when it exists, otherwise the game
   clock / scheduled time / status. */
static void fmt_game_center(const nba_game_t *g, char *out, size_t n)
{
    if (g->visitor_score >= 0 && g->home_score >= 0) {
        snprintf(out, n, "%d - %d", g->visitor_score, g->home_score);
    } else if (g->time[0]) {
        snprintf(out, n, "%s", g->time);
    } else if (g->status[0]) {
        snprintf(out, n, "%s", g->status);
    } else {
        snprintf(out, n, "--");
    }
}

static lv_obj_t *make_card_label(lv_obj_t *parent, const char *text,
                                 const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

/* A page of cards (~15 KB of LVGL pool) and the detail view (~12 KB) do not fit
   side by side comfortably, so the page is dropped while a game is open and
   rebuilt from the cache on the way back - no HTTP involved. */
static lv_obj_t *cards_scr_self = NULL;
static lv_timer_t *open_detail_timer = NULL;
static nba_game_t tapped_game;

static void open_detail_cb(lv_timer_t *t)
{
    (void)t;
    open_detail_timer = NULL;          /* LVGL frees a finished one-shot timer */
    cards_screen_open_detail(&tapped_game);
}

/* Coming back to the board: if its page was dropped for the detail view, ask
   the feed to render it again from the cache. */
static void cards_loaded_cb(lv_event_t *e)
{
    (void)e;
    if (cards_container && lv_obj_get_child_count(cards_container) == 0) {
        nba_service_rerender();
    }
}

/* Tap a card -> detail screen for that game. No extra API call: the games list
   response already carries the period-by-period score, so the detail view is
   built from what the cache holds. */
static void card_clicked_cb(lv_event_t *e)
{
    const nba_game_t *game = (const nba_game_t *)lv_event_get_user_data(e);

    /* Snapshot now: the cache is reordered by the feed worker, and the page of
       cards is about to be thrown away. */
    tapped_game = *game;

    /* Opening the screen inside the card's own event handler would delete the
       object LVGL is still sending the event to, so defer one timer tick. */
    if (!open_detail_timer) {
        open_detail_timer = lv_timer_create(open_detail_cb, 5, NULL);
        lv_timer_set_repeat_count(open_detail_timer, 1);   /* one-shot */
    }
}

/* Team logo in front of (away) / behind (home) the abbreviation on the card's
   centre row. The bitmaps are const flash tables (tools/make_team_logos.py),
   so the only runtime cost is the ~350 B widget; unknown team ids just skip
   the widget and the card falls back to abbreviation + name. */
/* Placeholder for teams whose bitmap is not baked (the NCAA table carries
   the power+group-of-five conferences only, to fit the app partition).
   A plain ring drawn at runtime into RGB565A8 (same packing as
   tools/make_team_logos.py emits): keeps card geometry identical to teams
   that have a real logo. */
#define LOGO_PH_W 22
static uint8_t s_ph_px[LOGO_PH_W * LOGO_PH_W * 3];
static lv_image_dsc_t s_placeholder;

static void placeholder_src(void)
{
    static lv_image_dsc_t dsc;
    static bool built = false;
    if (!built) {
        built = true;
        const int w = LOGO_PH_W, c = (w - 1) / 2;
        uint8_t *col = s_ph_px, *alp = s_ph_px + w * w * 2;
        for (int y = 0; y < w; y++) {
            for (int x = 0; x < w; x++) {
                int dx = x - c, dy = y - c;
                int rr = dx * dx + dy * dy;
                uint16_t px565 = 0;
                uint8_t a = 0;
                if (rr <= c * c) {
                    int dr = (c - 1) * (c - 1);
                    if (rr >= dr) {          /* ring: light grey, opaque */
                        px565 = 0xBDF7; a = 255;
                    } else if (rr <= (c - 5) * (c - 5)) {
                        px565 = 0x4208; a = 255;   /* dark inner disc */
                    }
                }
                col[(y * w + x) * 2]     = (uint8_t)(px565 & 0xFF);
                col[(y * w + x) * 2 + 1] = (uint8_t)(px565 >> 8);
                alp[y * w + x] = a;
            }
        }
        dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
        dsc.header.w = LOGO_PH_W;
        dsc.header.h = LOGO_PH_W;
        dsc.header.stride = LOGO_PH_W * 2;
        dsc.data_size = LOGO_PH_W * LOGO_PH_W * 3;
        dsc.data = s_ph_px;
        s_placeholder = dsc;
    }
}

static void add_card_logo(lv_obj_t *card, league_t league, int team_id)
{
    const lv_image_dsc_t *logo = game_logo(league, team_id);
    if (!logo) {
        placeholder_src();
        logo = &s_placeholder;
    }
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, logo);
    /* Clicks bubble up to the card, so tapping a logo still opens the detail. */
}

static void add_game_card(lv_obj_t *parent, const nba_game_t *g)
{
    bool have_score = (g->visitor_score >= 0 && g->home_score >= 0);
    bool live = (strcmp(g->status_state, "in_progress") == 0);

    /* Strip the theme's style set (padding, scroll, pressed states...) and
       keep only what the box needs: cards are the bulk of the LVGL pool and
       every inherited style costs memory. */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, UI_CARD_W, UI_CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COL_HEADER), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_pad_column(card, UI_CARD_COL_GAP, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_ofs_y(card, 2, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(UI_COL_SHADOW), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Cards are buttons now: tap one for the detail view. A click only fires if
       the finger did not turn into a scroll, so the list still scrolls. */
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COL_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, card_clicked_cb, LV_EVENT_CLICKED, (void *)(nba_game_t *)g);

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    /* Left-align the row and pin it to the top of the card at the same y as
       the date label: that is what lets the card be 46px instead of 56 - the
       bottom band carries the names, and the game clock sits between them. */
    lv_obj_set_style_pad_left(card, 8, 0);
    lv_obj_set_style_pad_top(card, UI_CARD_DATE_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(card, UI_CARD_NAME_H + UI_CARD_NAME_PAD_Y + 2, 0);

    /* Away team - score - home team (broadcast convention: visitor first).
       Long centre texts (ESPN tip lines like "11/10 - 6:00 PM EST") step down
       a font so the row stays inside the card. */
    uint32_t away_col = UI_COL_TEXT, home_col = UI_COL_TEXT;
    if (have_score && g->visitor_score != g->home_score) {
        away_col = (g->visitor_score > g->home_score) ? UI_COL_TEXT : UI_COL_PLACEHOLDER;
        home_col = (g->home_score > g->visitor_score) ? UI_COL_TEXT : UI_COL_PLACEHOLDER;
    }

    char away[8] = "--", home[8] = "--", center[24], date[16];
    if (g->visitor_abbr[0]) strncpy(away, g->visitor_abbr, sizeof(away) - 1);
    if (g->home_abbr[0])    strncpy(home, g->home_abbr, sizeof(home) - 1);
    fmt_game_center(g, center, sizeof(center));
    fmt_game_date(g->date, date, sizeof(date));

    add_card_logo(card, g->league, g->visitor_id);
    make_card_label(card, away, &lv_font_montserrat_16, away_col);

    uint32_t center_col = live ? UI_COL_OK : UI_COL_ACCENT;
    lv_obj_t *center_label = make_card_label(
        card, center,
        strlen(center) > 12 ? &lv_font_montserrat_16 : &lv_font_montserrat_20,
        center_col);
    /* Bound it: ESPN tip strings ("11/10 - 9:00 PM EST") would otherwise
       shove the home logo under the date label. */
    lv_label_set_long_mode(center_label, LV_LABEL_LONG_DOT);
    /* LONG_DOT only truncates with bounded size - and the bound must be a
       *max*, not a fixed box: a final score like "67 - 75" then stays its
       ~75px content width instead of shoving wide logos under the date. */
    lv_obj_set_style_max_width(center_label, UI_CARD_CENTER_W, 0);
    lv_obj_set_style_max_height(center_label, UI_CARD_CENTER_H, 0);
    lv_obj_set_style_text_align(center_label, LV_TEXT_ALIGN_CENTER, 0);

    make_card_label(card, home, &lv_font_montserrat_16, home_col);
    add_card_logo(card, g->league, g->home_id);

    /* Date, smaller, top-right. FLOATING keeps it out of the flex row. */
    lv_obj_t *date_label = make_card_label(card, date, &lv_font_montserrat_12,
                                           UI_COL_PLACEHOLDER);
    lv_obj_add_flag(date_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(date_label, LV_ALIGN_TOP_RIGHT, -UI_CARD_DATE_PAD_X, 0);

    /* Game clock, along the bottom between the two name labels (the top band
       belongs to the main row now, so the old top-left slot would collide
       with it). Scheduled/canceled games skip it: their state already sits
       in the centre text. */
    if (g->status[0] && live) {
        uint32_t status_col = UI_COL_OK;
        lv_obj_t *status_label = make_card_label(card, g->status,
                                                 &lv_font_montserrat_12,
                                                 status_col);
        lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(status_label, 70);
        lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, UI_CARD_NAME_ROW_Y);
    }

    /* Full team names along the bottom: three letters are no use to someone
       reading the board from across the room. Fixed width + LONG_DOT keeps even
       "Minnesota Timberwolves" inside its half of the card (the two labels are
       140px with 8px side insets, so they cannot touch). */
    if (g->visitor_name[0]) {
        lv_obj_t *name = make_card_label(card, g->visitor_name,
                                         &lv_font_montserrat_12, away_col);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(name, UI_CARD_NAME_W, UI_CARD_NAME_H);
        lv_obj_add_flag(name, LV_OBJ_FLAG_FLOATING);
        /* TOP-anchored with an explicit y: floating children align against
           the *padding* box, and the row's pad_bottom (17) would otherwise
           drop BOTTOM anchors into the row band itself. */
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, UI_CARD_DATE_PAD_X,
                     UI_CARD_NAME_ROW_Y);
    }
    if (g->home_name[0]) {
        lv_obj_t *name = make_card_label(card, g->home_name,
                                         &lv_font_montserrat_12, home_col);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_size(name, UI_CARD_NAME_W, UI_CARD_NAME_H);
        lv_obj_add_flag(name, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(name, LV_ALIGN_TOP_RIGHT, -UI_CARD_DATE_PAD_X,
                     UI_CARD_NAME_ROW_Y);
    }
}

/* Pager strip: [<] Oct 14 - Oct 16 ... page 3 [>] */
static void set_page_bar(const cards_page_t *page)
{
    if (!page || !page_date_label) return;

    char text[40];
    if (page->iso_newest && page->iso_newest[0]) {
        char newest[16], oldest[16];
        fmt_game_date(page->iso_newest, newest, sizeof(newest));
        fmt_game_date(page->iso_oldest, oldest, sizeof(oldest));
        if (strcmp(newest, oldest) != 0) {
            snprintf(text, sizeof(text), "%s - %s", oldest, newest);
        } else {
            snprintf(text, sizeof(text), "%s", newest);
        }
    } else {
        snprintf(text, sizeof(text), "--");
    }
    lv_label_set_text(page_date_label, text);

    char index[16];
    snprintf(index, sizeof(index), "page %d", page->page);
    lv_label_set_text(page_index_label, index);

    /* Grey out (and disarm) the arrow that would leave the range. */
    lv_obj_set_style_text_color(page_prev_btn,
                                lv_color_hex(page->has_earlier ? UI_COL_TEXT
                                                               : UI_COL_SUBTLE),
                                0);
    lv_obj_set_style_text_color(page_next_btn,
                                lv_color_hex(page->has_later ? UI_COL_TEXT
                                                             : UI_COL_SUBTLE),
                                0);
    if (page->has_earlier) {
        lv_obj_add_flag(page_prev_btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_clear_flag(page_prev_btn, LV_OBJ_FLAG_CLICKABLE);
    }
    if (page->has_later) {
        lv_obj_add_flag(page_next_btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_clear_flag(page_next_btn, LV_OBJ_FLAG_CLICKABLE);
    }
}

void cards_screen_show_games(const nba_game_t *games, int count,
                             const cards_page_t *page)
{
    set_page_bar(page);

    if (!cards_container || !games || count <= 0) {
        cards_screen_show_message("No games to display", NULL);
        return;
    }

    lv_obj_clean(cards_container);
    for (int i = 0; i < count; i++) {
        add_game_card(cards_container, &games[i]);
    }
    lv_obj_scroll_to_y(cards_container, 0, LV_ANIM_OFF);

    /* LVGL widgets come from LVGL's own pool (CONFIG_LV_MEM_SIZE_KILOBYTES),
       so ESP free heap says nothing about rendering headroom. */
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "Rendered %d card(s): lv pool %u%% used (%u%% frag), ESP heap %u",
             count, mon.used_pct, mon.frag_pct, esp_get_free_heap_size());
}

void cards_screen_show_message(const char *msg, const cards_page_t *page)
{
    if (!cards_container) return;

    set_page_bar(page);

    lv_obj_clean(cards_container);
    lv_obj_t *label = lv_label_create(cards_container);
    lv_label_set_text(label, msg && msg[0] ? msg : "No games to display");
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COL_PLACEHOLDER), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -14);

    /* A quiet slate also happens legitimately (off-season, or the ESPN feed
       only carries network-covered games) - say where to look. */
    lv_obj_t *hint = lv_label_create(cards_container);
    lv_label_set_text(hint, "Check Wi-Fi, or try another tab");
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COL_SUBTLE), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(hint, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
}

/* Header icon button: flat against the header bar. The default theme's
   button shadow shows up as a grey smudge on the dark header, so kill it. */
static lv_obj_t *make_header_button(lv_obj_t *parent, const char *symbol)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, UI_BTN_MD, UI_BTN_MD);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_HEADER), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    return btn;
}

/* Flat arrow button for the date pager strip (smaller than the header icons). */
static lv_obj_t *make_page_button(lv_obj_t *parent, const char *symbol)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, UI_PAGEBTN_W, 22);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_BG), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    /* Colour the button, not the label: set_page_bar() dims the arrow when the
       page range runs out, and an explicit label colour would win over that. */
    lv_obj_set_style_text_color(btn, lv_color_hex(UI_COL_TEXT), LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    return btn;
}

lv_obj_t *cards_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), UI_HEADER_H_CARDS);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COL_HEADER), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    /* Theme pad_all (~13px) would steal 26px of the row and wrap the badge. */
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* League selector: NBA | MBB | WBB. Which tab is lit is which feed the
       board shows - it replaces the old static "NBA SCORES" title. Plain
       clickable labels (not lv_btn): buttons cost a second widget (their
       child label) each, and the pool is exactly the scarce resource here
       (12-card stress renders died at 64 KB once buttons were added).
       The look lives in ONE shared style: per-object style properties add
       up fast, and 12 cards + tabs sit at the very edge of the pool. */
    static lv_style_t tab_style;
    static bool tab_style_ready = false;
    if (!tab_style_ready) {
        tab_style_ready = true;
        lv_style_init(&tab_style);
        lv_style_set_radius(&tab_style, 6);
        lv_style_set_bg_opa(&tab_style, LV_OPA_COVER);
        lv_style_set_text_font(&tab_style, &lv_font_montserrat_12);
        lv_style_set_text_align(&tab_style, LV_TEXT_ALIGN_CENTER);
        /* (no state selector here: lv_style_set_* is state-free - the pressed
           shade goes on the object, see below.) */
    }
    for (int i = 0; i < 4; i++) {
        lv_obj_t *tab = lv_label_create(header);
        lv_label_set_text(tab, TAB_LABELS[i]);
        lv_obj_set_size(tab, UI_TAB_W, UI_TAB_H);
        lv_obj_set_style_pad_top(tab, 4, 0);   /* label has no built-in padding: */
        lv_obj_set_style_pad_bottom(tab, 0, 0); /* give the text a touchable box */
        lv_obj_set_style_bg_color(tab, lv_color_hex(UI_COL_PRESSED),
                                  LV_STATE_PRESSED);
        lv_obj_add_style(tab, &tab_style, 0);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_margin_left(tab, (i == 0) ? 10 : UI_TAB_GAP, 0);

        lv_obj_add_event_cb(tab, league_tab_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        league_tabs[i] = tab;
    }
    style_league_tabs();

    wifi_status_label = lv_label_create(header);
    lv_label_set_text(wifi_status_label, "");
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(UI_COL_SUBTLE), 0);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_left(wifi_status_label, 8, 0);
    lv_label_set_long_mode(wifi_status_label, LV_LABEL_LONG_CLIP);
    /* Swallow the leftover width so the icon buttons stay pinned to the right
       edge whatever the status text is ("Disconnected" would otherwise push
       the gear off-screen). */
    lv_obj_set_flex_grow(wifi_status_label, 1);
    lv_obj_set_style_text_align(wifi_status_label, LV_TEXT_ALIGN_LEFT, 0);

    lv_obj_t *refresh_btn = make_header_button(header, LV_SYMBOL_REFRESH);
    lv_obj_set_style_pad_left(refresh_btn, 4, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_btn = make_header_button(header, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_pad_right(settings_btn, 12, 0);
    lv_obj_add_event_cb(settings_btn, settings_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* ---- date pager strip ---- */
    lv_obj_t *page_bar = lv_obj_create(scr);
    lv_obj_set_size(page_bar, LV_PCT(100), UI_PAGEBAR_H);
    lv_obj_set_pos(page_bar, 0, UI_HEADER_H_CARDS);
    lv_obj_set_style_bg_color(page_bar, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_border_width(page_bar, 0, 0);
    lv_obj_set_style_radius(page_bar, 0, 0);
    lv_obj_set_style_pad_all(page_bar, 0, 0);
    lv_obj_clear_flag(page_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(page_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    page_prev_btn = make_page_button(page_bar, LV_SYMBOL_LEFT);
    lv_obj_set_style_pad_left(page_prev_btn, 12, 0);
    lv_obj_add_event_cb(page_prev_btn, page_earlier_btn_cb, LV_EVENT_CLICKED, NULL);

    page_date_label = lv_label_create(page_bar);
    lv_label_set_text(page_date_label, "--");
    lv_obj_set_style_text_color(page_date_label, lv_color_hex(UI_COL_TEXT), 0);
    lv_obj_set_style_text_font(page_date_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_left(page_date_label, 10, 0);
    /* A page can straddle a date boundary ("Oct 14 - Oct 16"): cap the width so
       the page counter always has room. */
    lv_obj_set_width(page_date_label, UI_PAGEBAR_DATE_W);
    lv_label_set_long_mode(page_date_label, LV_LABEL_LONG_DOT);

    page_index_label = lv_label_create(page_bar);
    lv_label_set_text(page_index_label, "");
    lv_obj_set_style_text_color(page_index_label, lv_color_hex(UI_COL_SUBTLE), 0);
    lv_obj_set_style_text_font(page_index_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_right(page_index_label, 6, 0);
    lv_obj_set_flex_grow(page_index_label, 1);
    lv_obj_set_style_text_align(page_index_label, LV_TEXT_ALIGN_RIGHT, 0);

    page_next_btn = make_page_button(page_bar, LV_SYMBOL_RIGHT);
    lv_obj_add_event_cb(page_next_btn, page_later_btn_cb, LV_EVENT_CLICKED, NULL);

    /* ---- card list ---- */
    cards_container = lv_obj_create(scr);
    lv_obj_set_size(cards_container, LV_PCT(100),
                    LV_VER_RES - UI_HEADER_H_CARDS - UI_PAGEBAR_H);
    lv_obj_set_pos(cards_container, 0, UI_HEADER_H_CARDS + UI_PAGEBAR_H);
    lv_obj_set_style_bg_color(cards_container, lv_color_hex(UI_COL_BG), 0);
    lv_obj_set_style_border_width(cards_container, 0, 0);
    lv_obj_set_style_radius(cards_container, 0, 0);
    lv_obj_set_style_pad_all(cards_container, 0, 0);
    lv_obj_set_style_pad_row(cards_container, UI_CARD_GAP, 0);
    lv_obj_set_flex_flow(cards_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cards_container, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(cards_container, LV_DIR_VER);

    cards_scr_self = scr;
    /* The board is re-rendered from cache when it comes back with an empty
       container (i.e. after the detail view borrowed the LVGL pool). */
    lv_obj_add_event_cb(scr, cards_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

    cards_screen_show_message("No games to display", NULL);

    return scr;
}

/* True while the board is the screen on screen: the feed skips pushing a page
   of cards into a hidden screen (it would only burn pool and CPU). */
bool cards_screen_page_visible(void)
{
    return cards_scr_self && lv_screen_active() == cards_scr_self;
}

void cards_screen_open_detail(const nba_game_t *game)
{
    if (!game) {
        return;
    }
    if (cards_container) {
        lv_obj_clean(cards_container);
    }
    game_detail_screen_show(game);
}
