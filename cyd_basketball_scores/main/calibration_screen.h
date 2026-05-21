#pragma once

#include "lvgl.h"

typedef void (*calibration_done_cb_t)(void);

lv_obj_t *calibration_screen_create(void);
void calibration_screen_set_done_cb(calibration_done_cb_t cb);
void calibration_screen_set_back_scr(lv_obj_t *scr);
