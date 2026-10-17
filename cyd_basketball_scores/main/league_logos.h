#pragma once

#include "lvgl.h"
#include "nba_api.h"
#include "team_logos.h"

/* Logo accessor picked by the feed the game came from. The ids are separate,
   overlapping namespaces (balldontlie NBA/WNBA, ESPN school ids for college),
   so a raw team_id can only be looked up together with its league. */
const lv_image_dsc_t *game_logo(league_t league, int team_id);
