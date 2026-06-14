#pragma once

#include "lvgl.h"

lv_obj_t *wifi_screen_create(void);
void wifi_screen_set_settings_scr(lv_obj_t *scr);
void wifi_screen_set_cards_scr(lv_obj_t *scr);
void wifi_screen_refresh_saved_networks(void);
