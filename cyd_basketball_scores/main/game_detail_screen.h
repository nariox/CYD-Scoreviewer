#pragma once

#include "lvgl.h"
#include "nba_api.h"

/* Game detail screen: tap a score card to see the box score by period, the
   game date/tip-off and, while a game runs, the timeouts left.
   The widgets are built on the first tap and reused afterwards, so the boot
   flow pays no LVGL pool for a screen that may never be opened. */

/* Fill the screen with a copy of `game` and load it.
   Call from LVGL task context (a click callback) or with the port lock held. */
void game_detail_screen_show(const nba_game_t *game);

/* Back button target (the cards screen). */
void game_detail_screen_set_back_scr(lv_obj_t *scr);

/* Same as tapping the back button. Call from LVGL task context, or with the
   LVGL port lock held. */
void game_detail_screen_back(void);
