#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "nba_api.h"
#include "wifi_backend.h"

/* One page of the board: a fixed number of games, newest first. A busy slate
   spills over several pages; a quiet stretch covers several days, so the page
   carries the date range it spans. */
typedef struct {
    const char *iso_newest;   /* newest game on this page, NULL = no games */
    const char *iso_oldest;   /* oldest game on this page */
    int  page;                /* 1-based, 1 = most recent games */
    bool has_earlier;         /* more history can be loaded -> [<<] enabled */
    bool has_later;           /* a newer page exists       -> [>>] enabled */
} cards_page_t;

lv_obj_t *cards_screen_create(void);
void cards_screen_set_settings_scr(lv_obj_t *scr);
void cards_screen_update_wifi_status(wifi_state_t state);

/* Replace the card list with one score box per game and update the date
   pager. page may be NULL to leave the pager as it is.
   Must be called with the LVGL port lock held (worker tasks). */
void cards_screen_show_games(const nba_game_t *games, int count,
                             const cards_page_t *page);

/* Replace the card list with a single centered message
   ("Loading...", "No games", errors). Same locking rule as above. */
void cards_screen_show_message(const char *msg, const cards_page_t *page);
