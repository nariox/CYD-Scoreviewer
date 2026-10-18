/* Minimal host stub: nvs_settings.h pulls touch_integration.h, whose only
   need here is the touch-handle typedef. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct esp_lcd_touch *esp_lcd_touch_handle_t;
typedef struct {
    uint16_t x;
    uint16_t y;
} esp_lcd_touch_point_data_t;

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp);
esp_err_t esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x,
                                        uint16_t *y, uint16_t *strength,
                                        uint8_t *point_num, uint8_t max_point_num);
