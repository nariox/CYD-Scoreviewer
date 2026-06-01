#pragma once

#include "driver/gpio.h"

/* ---- LCD (ST7789, SPI2) ---- */
#define LCD_H_RES           320
#define LCD_V_RES           240
#define LCD_BUF_LINES       30
#define LCD_DRAWBUF_SIZE    (LCD_H_RES * LCD_BUF_LINES)
#define LCD_DOUBLE_BUFFER   true
#define LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)
#define LCD_CMD_BITS        8
#define LCD_PARAM_BITS      8
#define LCD_HOST            SPI2_HOST

#define PIN_LCD_SCLK        14
#define PIN_LCD_MOSI        13
#define PIN_LCD_MISO        12
#define PIN_LCD_DC          2
#define PIN_LCD_CS          15
#define PIN_LCD_RST         4
#define PIN_LCD_BL          21

/* ---- Touch (XPT2046, SPI3) ---- */
#define TOUCH_HOST          SPI3_HOST
#define TOUCH_X_DIM         240
#define TOUCH_Y_DIM         320
#define TOUCH_X_RES_MIN     208
#define TOUCH_X_RES_MAX     3879
#define TOUCH_Y_RES_MIN     179
#define TOUCH_Y_RES_MAX     3818

#define PIN_TOUCH_MOSI      32
#define PIN_TOUCH_MISO      39
#define PIN_TOUCH_SCLK      25
#define PIN_TOUCH_CS        33
#define PIN_TOUCH_IRQ       36

/* ---- LEDC (backlight) ---- */
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        0
#define LEDC_FREQ_HZ        (5000)
#define LEDC_DUTY_MAX       255

/* ---- Calibration ---- */
#define CALIB_BUTTON_SIZE    30
#define CALIB_EDGE_GAP       60
#define CALIB_SANITY_MIN     1536
#define CALIB_SANITY_MAX     2559

/* ---- LVGL ---- */
#define LVGL_TICK_PERIOD_MS 2
