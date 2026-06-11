#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include "hardware.h"

typedef struct {
    uint16_t x;
    uint16_t y;
} touch_raw_adc_t;

typedef struct {
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
    bool swap_xy;
} calibration_data_t;

esp_err_t touch_integration_init(esp_lcd_touch_handle_t *tp);
void get_calibration_data(uint16_t *x_min, uint16_t *x_max, uint16_t *y_min, uint8_t *y_max);
void touch_integration_calibrate(touch_raw_adc_t *samples, calibration_data_t *out);
void touch_integration_apply_calibration(calibration_data_t *cal);
void touch_integration_get_raw_adc(uint16_t *x, uint16_t *y);
