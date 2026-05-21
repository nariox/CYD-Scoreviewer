#pragma once

#include "esp_err.h"
#include "lvgl.h"

//#define TOUCH_X_RES_MIN 310
//#define TOUCH_X_RES_MAX 3870
//#define TOUCH_Y_RES_MIN 260
//#define TOUCH_Y_RES_MAX 3900
#define TOUCH_X_DIM 240
#define TOUCH_Y_DIM 320
#define TOUCH_X_RES_MIN 208
#define TOUCH_X_RES_MAX 3879
#define TOUCH_Y_RES_MIN 179
#define TOUCH_Y_RES_MAX 3818

esp_err_t touch_integration_init(lv_display_t *disp, int8_t spi_host_num, int8_t mosi_io_num, int8_t miso_io_num, int8_t sclk_io_num, int8_t cs_io_num, int8_t int_io_num);
