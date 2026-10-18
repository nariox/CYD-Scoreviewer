#pragma once

#include "lvgl.h"

lv_obj_t *splash_screen_create(void);

/* Update the boot stage line ("Connecting to Wi-Fi..." etc). Must hold the
   port lock unless called from LVGL task context. */
void splash_screen_set_status(const char *text);

/* Is the splash the screen on screen right now? (LVGL task context, or
   under the port lock.) */
bool splash_screen_is_current(void);
