#pragma once

#include "lvgl.h"

lv_obj_t *settings_screen_create(void);
void settings_screen_set_wifi_scr(lv_obj_t *scr);
void settings_screen_set_cards_scr(lv_obj_t *scr);
