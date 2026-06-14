#pragma once

#include "lvgl.h"
#include "wifi_backend.h"

lv_obj_t *cards_screen_create(void);
void cards_screen_set_settings_scr(lv_obj_t *scr);
void cards_screen_update_wifi_status(wifi_state_t state);
