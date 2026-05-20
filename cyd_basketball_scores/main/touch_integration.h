#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t touch_integration_init(int8_t spi_host_num, int8_t mosi_io_num, int8_t miso_io_num, int8_t sclk_io_num, int8_t cs_io_num, int8_t int_io_num);
void touch_integration_set_coords_label(lv_obj_t *label);
