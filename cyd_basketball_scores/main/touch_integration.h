#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include "hardware.h"

esp_err_t touch_integration_init(esp_lcd_touch_handle_t *tp);
