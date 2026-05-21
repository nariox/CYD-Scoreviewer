#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t lcd_init(void);
lv_display_t *lcd_get_display(void);
esp_err_t lcd_brightness_set(uint8_t duty);
