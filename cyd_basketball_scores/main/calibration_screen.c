#include "calibration_screen.h"
#include "touch_test_screen.h"
#include "touch_integration.h"
#include "nvs_settings.h"
#include <stdio.h>
#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "calibration";

static touch_raw_adc_t s_samples[4];
static uint8_t s_tapped_count = 0;
static uint32_t s_last_tap_ms = 0;

static lv_obj_t *s_scr;
static lv_obj_t *s_coords_label;
static lv_obj_t *s_done_btn;
static lv_obj_t *s_crosshairs[4];
static lv_obj_t *s_instruction;
static calibration_done_cb_t s_done_cb = NULL;
static lv_obj_t *s_back_scr = NULL;

#define CALIB_DEBOUNCE_MS 500

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static const int corner_x[] = {CALIB_EDGE_GAP, CALIB_EDGE_GAP,
                               LCD_H_RES - CALIB_EDGE_GAP, LCD_H_RES - CALIB_EDGE_GAP};
static const int corner_y[] = {CALIB_EDGE_GAP, LCD_V_RES - CALIB_EDGE_GAP,
                               LCD_V_RES - CALIB_EDGE_GAP, CALIB_EDGE_GAP};

static const char *s_corner_names[] = {
    "Tap: Top-Left",
    "Tap: Bottom-Left",
    "Tap: Bottom-Right",
    "Tap: Top-Right",
};

static void highlight_next_crosshair(void)
{
    for (int i = 0; i < 4; i++) {
        if (i <= s_tapped_count) {
            lv_obj_set_style_border_color(s_crosshairs[i], lv_color_hex(0x00b4d8), 0);
        } else {
            lv_obj_set_style_border_color(s_crosshairs[i], lv_color_hex(0x555555), 0);
        }
    }
}

static const uint16_t s_quadrant_mid = 2047;

static const uint8_t s_expected_x_lo[] = {1, 1, 0, 0};
static const uint8_t s_expected_y_lo[] = {1, 0, 0, 1};

static inline bool tap_in_expected_quadrant(uint8_t idx, uint16_t raw_x, uint16_t raw_y)
{
    if (s_expected_x_lo[idx]) {
        if (raw_x > s_quadrant_mid) return false;
    } else {
        if (raw_x < s_quadrant_mid) return false;
    }
    if (s_expected_y_lo[idx]) {
        if (raw_y > s_quadrant_mid) return false;
    } else {
        if (raw_y < s_quadrant_mid) return false;
    }
    return true;
}

static bool s_screen_ready = false;

static void undo_last_tap(void)
{
    if (s_tapped_count == 0) {
        ESP_LOGD(TAG, "Undo: no taps to undo");
        return;
    }

    ESP_LOGD(TAG, "Undo: removing tap %u (X=%u Y=%u)",
             s_tapped_count - 1, s_samples[s_tapped_count - 1].x, s_samples[s_tapped_count - 1].y);

    s_tapped_count--;

    lv_obj_set_style_bg_color(s_crosshairs[s_tapped_count], lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_color(s_crosshairs[s_tapped_count], lv_color_hex(0x555555), 0);

    if (s_tapped_count > 0) {
        lv_label_set_text(s_instruction, s_corner_names[s_tapped_count]);
    } else {
        lv_label_set_text(s_instruction, s_corner_names[0]);
    }
    highlight_next_crosshair();
    lv_obj_add_flag(s_done_btn, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGD(TAG, "Undo: now at tap %u, instruction='%s'", s_tapped_count, s_corner_names[s_tapped_count]);
}

static void screen_tap_cb(lv_event_t *e)
{
    if (!s_screen_ready) {
        ESP_LOGD(TAG, "screen_tap_cb: screen not ready, skipping");
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_LONG_PRESSED) {
        if (s_tapped_count == 0) {
            ESP_LOGD(TAG, "Long press: no taps to undo");
            return;
        }
        ESP_LOGI(TAG, "Long press: undoing tap %u", s_tapped_count);
        undo_last_tap();
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_CANCEL) {
        return;
    }

    if (s_tapped_count >= 4) return;

    if (code != LV_EVENT_CLICKED) return;

    uint32_t now = lv_tick_get();
    if (now - s_last_tap_ms < CALIB_DEBOUNCE_MS) {
        return;
    }
    s_last_tap_ms = now;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    (void)point;

    uint16_t raw_x, raw_y;
    touch_integration_get_raw_adc(&raw_x, &raw_y);

    if (!tap_in_expected_quadrant(s_tapped_count, raw_x, raw_y)) {
        ESP_LOGW(TAG, "Tap %u ignored: X=%u Y=%u outside expected quadrant",
                 s_tapped_count, raw_x, raw_y);
        return;
    }

    s_samples[s_tapped_count].x = raw_x;
    s_samples[s_tapped_count].y = raw_y;

    lv_obj_set_style_bg_color(s_crosshairs[s_tapped_count], lv_color_hex(0x00b4d8), 0);
    lv_obj_set_style_border_color(s_crosshairs[s_tapped_count], lv_color_hex(0x00b4d8), 0);

    char buf[64];
    snprintf(buf, sizeof(buf), "X: %u  Y: %u", raw_x, raw_y);
    lv_label_set_text(s_coords_label, buf);

    s_tapped_count++;

    if (s_tapped_count < 4) {
        lv_label_set_text(s_instruction, s_corner_names[s_tapped_count]);
        highlight_next_crosshair();
    }

    if (s_tapped_count >= 4) {
        lv_obj_clear_flag(s_done_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void done_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_done_btn, LV_OBJ_FLAG_HIDDEN);

    calibration_data_t cal;
//    touch_integration_calibrate(s_samples, &cal);

    float mean_x=0, mean_y=0;
    for(int i = 0; i < 4; i++){
        mean_x += s_samples[i].x;
        mean_y += s_samples[i].y;
    }
    mean_x *= 0.25;
    mean_y *= 0.25;

    float max_x=0, max_y=0;
    float min_x=0, min_y=0;
    int count_x=0, count_y=0;
    for(int i = 0; i < 4; i++){
        ESP_LOGW(TAG, "Sample [%u] = X -> %u and Y -> %u", i, s_samples[i].x, s_samples[i].y);
    }
    for(int i = 0; i < 4; i++){
        if(s_samples[i].x > mean_x){
            max_x += s_samples[i].x;
            count_x++;
        }
        else{
            min_x += s_samples[i].x;
        }
        if(s_samples[i].y > mean_y){
            max_y += s_samples[i].y;
            count_y++;
        }
        else{
            min_y += s_samples[i].y;
        }
    }
    min_y *= 0.5; max_y *= 0.5; min_x *= 0.5; max_x *= 0.5;
    ESP_LOGW(TAG, "Raw Calibration data: mean_x=%g, mean_y=%g x_min=%g x_max=%g y_min=%g y_max=%g",
        mean_x, mean_y, min_x, max_x, min_y, max_y);
    min_y = mean_y + ((min_y - mean_y) * LCD_V_RES)/ (LCD_V_RES - 2*CALIB_EDGE_GAP);
    min_x = mean_x + ((min_x - mean_x) * LCD_H_RES)/ (LCD_H_RES - 2*CALIB_EDGE_GAP);
    max_y = mean_y + ((max_y - mean_y) * LCD_V_RES)/ (LCD_V_RES - 2*CALIB_EDGE_GAP);
    max_x = mean_x + ((max_x - mean_x) * LCD_H_RES)/ (LCD_H_RES - 2*CALIB_EDGE_GAP);
    ESP_LOGW(TAG, "Proc'd Calibration data: mean_x=%g, mean_y=%g x_min=%g x_max=%g y_min=%g y_max=%g",
        mean_x, mean_y, min_x, max_x, min_y, max_y);
    cal.x_min = round(clampf(min_x,0,4095));
    cal.x_max = round(clampf(max_x,0,4095));
    cal.y_min = round(clampf(min_y,0,4095));
    cal.y_max = round(clampf(max_y,0,4095));

    if(count_x != 2 || count_y != 2){
        ESP_LOGW(TAG, "Calibration failed: too many points on the same side");
        lv_label_set_text(s_coords_label, "Calibration invalid!");
        lv_obj_set_style_text_color(s_coords_label, lv_color_hex(0xff4444), 0);
        return;
    }

    touch_integration_apply_calibration(&cal);

    /* Save calibration to NVS */
    nvs_settings_save_calibration(&cal);
    nvs_settings_save_calibration_data(true);

    ESP_LOGI(TAG, "Calibration saved: min_x=%u max_x=%u min_y=%u max_y=%u",
             (int)round(min_x), (int)round(max_x), (int)round(min_y), (int)round(max_y));

    lv_label_set_text(s_coords_label, "Calibration done!");
    lv_obj_set_style_text_color(s_coords_label, lv_color_hex(0x44ff44), 0);

    if (s_done_cb) {
        s_done_cb();
    }
}

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_back_scr) {
        lv_scr_load(s_back_scr);
    } else if (s_done_cb) {
        s_done_cb();
    }
}

static void reset_calibration_state(void)
{
    s_tapped_count = 0;
    s_last_tap_ms = 0;
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_crosshairs[i], lv_color_hex(0x0f3460), 0);
        lv_obj_set_style_border_color(s_crosshairs[i], lv_color_hex(0x555555), 0);
    }
    lv_label_set_text(s_coords_label, "X: ---  Y: ---");
    lv_obj_set_style_text_color(s_coords_label, lv_color_hex(0x00b4d8), 0);
    lv_label_set_text(s_instruction, s_corner_names[0]);
    lv_obj_add_flag(s_done_btn, LV_OBJ_FLAG_HIDDEN);
}

void calibration_screen_reset(void)
{
    reset_calibration_state();
}

lv_obj_t *calibration_screen_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_size(header, LV_PCT(100), 40);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(header, 8, 0);
    lv_obj_set_style_pad_right(header, 8, 0);

    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 28, 28);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_16, 0);
    lv_obj_center(back_label);

    lv_obj_add_event_cb(back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *header_title = lv_label_create(header);
    lv_label_set_text(header_title, "Touch Calibration");
    lv_obj_set_style_text_color(header_title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(header_title, &lv_font_montserrat_16, 0);

    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100) - 40);
    lv_obj_set_pos(content, 0, 40);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);

    s_instruction = lv_label_create(content);
    lv_label_set_text(s_instruction, s_corner_names[0]);
    lv_obj_set_style_text_color(s_instruction, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_instruction, &lv_font_montserrat_16, 0);

    lv_obj_t *info_container = lv_obj_create(content);
    lv_obj_set_size(info_container, 140, 25);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(info_container, 1, 0);
    lv_obj_set_style_border_color(info_container, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(info_container, 8, 0);
    lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(info_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(info_container, 2, 0);

    s_coords_label = lv_label_create(info_container);
    lv_label_set_text(s_coords_label, "X: ---  Y: ---");
    lv_obj_set_style_text_color(s_coords_label, lv_color_hex(0x00b4d8), 0);
    lv_obj_set_style_text_font(s_coords_label, &lv_font_montserrat_14, 0);

    for (int i = 0; i < 4; i++) {
        s_crosshairs[i] = lv_obj_create(s_scr);
        lv_obj_set_size(s_crosshairs[i], CALIB_BUTTON_SIZE, CALIB_BUTTON_SIZE);
        lv_obj_set_pos(s_crosshairs[i], corner_x[i] - CALIB_BUTTON_SIZE/2, corner_y[i] - CALIB_BUTTON_SIZE/2);
        lv_obj_set_style_bg_color(s_crosshairs[i], lv_color_hex(0x0f3460), 0);
        lv_obj_set_style_border_width(s_crosshairs[i], 3, 0);
        lv_obj_set_style_border_color(s_crosshairs[i], lv_color_hex(0x555555), 0);
        lv_obj_set_style_radius(s_crosshairs[i], CALIB_BUTTON_SIZE/2, 0);
        lv_obj_clear_flag(s_crosshairs[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *cross_label = lv_label_create(s_crosshairs[i]);
        char point_buf[8];
        snprintf(point_buf, sizeof(point_buf), "%d", i + 1);
        lv_label_set_text(cross_label, point_buf);
        lv_obj_set_style_text_color(cross_label, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(cross_label, &lv_font_montserrat_16, 0);
        lv_obj_center(cross_label);
    }

    s_done_btn = lv_btn_create(content);
    lv_obj_set_size(s_done_btn, 140 , 25);
    lv_obj_add_flag(s_done_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(s_done_btn, lv_color_hex(0x00b4d8), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_done_btn, 0, 0);
    lv_obj_set_style_radius(s_done_btn, 8, 0);
    lv_obj_clear_flag(s_done_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *done_label = lv_label_create(s_done_btn);
    lv_label_set_text(done_label, "Done & Save");
    lv_obj_set_style_text_color(done_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(done_label, &lv_font_montserrat_16, 0);
    lv_obj_center(done_label);

    lv_obj_add_event_cb(s_done_btn, done_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(content, screen_tap_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(content, screen_tap_cb, LV_EVENT_LONG_PRESSED, NULL);

    for (int i = 0; i < 4; i++) {
        lv_obj_add_event_cb(s_crosshairs[i], screen_tap_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(s_crosshairs[i], screen_tap_cb, LV_EVENT_LONG_PRESSED, NULL);
    }

    reset_calibration_state();

    s_screen_ready = true;
    return s_scr;
}

void calibration_screen_set_done_cb(calibration_done_cb_t cb)
{
    s_done_cb = cb;
}

void calibration_screen_set_back_scr(lv_obj_t *scr)
{
    s_back_scr = scr;
}
