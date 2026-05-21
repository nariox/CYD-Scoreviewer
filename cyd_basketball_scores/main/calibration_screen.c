#include "calibration_screen.h"
#include <stdio.h>
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "calibration";

static lv_obj_t *back_scr = NULL;
static calibration_done_cb_t done_cb = NULL;
static lv_obj_t *coords_label = NULL;
static lv_obj_t *raw_coords_label = NULL;
static lv_obj_t *crosshairs[5];
static uint8_t tapped_count = 0;

static const int cal_points_x[] = {20, 160, 300, 160, 20};
static const int cal_points_y[] = {20, 20, 20, 120, 220};

static void touch_calibrate_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    ESP_LOGI(TAG, "Calibration touch: x=%d, y=%d", point.x, point.y);

    char buf[64];
    snprintf(buf, sizeof(buf), "X: %ld  Y: %ld", (long)point.x, (long)point.y);
    lv_label_set_text(coords_label, buf);

    snprintf(buf, sizeof(buf), "Raw: ---  ---");
    lv_label_set_text(raw_coords_label, buf);

    if (tapped_count < 5) {
        lv_obj_set_style_bg_color(crosshairs[tapped_count], lv_color_hex(0x00b4d8), 0);
        tapped_count++;

        if (tapped_count >= 5) {
            char done_buf[32];
            snprintf(done_buf, sizeof(done_buf), "Tapped all 5 points");
            lv_label_set_text(raw_coords_label, done_buf);
        }
    }
}

static void back_btn_event_cb(lv_event_t *e)
{
    if (back_scr) {
        lv_scr_load(back_scr);
    }
    if (done_cb) {
        done_cb();
    }
}

lv_obj_t *calibration_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x16213e), 0);

    lv_obj_t *header = lv_obj_create(scr);
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

    lv_obj_add_event_cb(back_btn, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *header_title = lv_label_create(header);
    lv_label_set_text(header_title, "Touch Calibration");
    lv_obj_set_style_text_color(header_title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(header_title, &lv_font_montserrat_16, 0);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100) - 40);
    lv_obj_set_pos(content, 0, 40);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);

    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 8, 0);

    lv_obj_t *instruction = lv_label_create(content);
    lv_label_set_text(instruction, "Tap each crosshair point");
    lv_obj_set_style_text_color(instruction, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(instruction, &lv_font_montserrat_12, 0);

    lv_obj_t *info_container = lv_obj_create(content);
    lv_obj_set_size(info_container, 280, 50);
    lv_obj_set_style_bg_color(info_container, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(info_container, 1, 0);
    lv_obj_set_style_border_color(info_container, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(info_container, 8, 0);
    lv_obj_clear_flag(info_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(info_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(info_container, 2, 0);

    coords_label = lv_label_create(info_container);
    lv_label_set_text(coords_label, "X: ---  Y: ---");
    lv_obj_set_style_text_color(coords_label, lv_color_hex(0x00b4d8), 0);
    lv_obj_set_style_text_font(coords_label, &lv_font_montserrat_14, 0);

    raw_coords_label = lv_label_create(info_container);
    lv_label_set_text(raw_coords_label, "Raw: ---  ---");
    lv_obj_set_style_text_color(raw_coords_label, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(raw_coords_label, &lv_font_montserrat_12, 0);

    for (int i = 0; i < 5; i++) {
        crosshairs[i] = lv_obj_create(scr);
        lv_obj_set_size(crosshairs[i], 40, 40);
        lv_obj_set_pos(crosshairs[i], cal_points_x[i] - 20, cal_points_y[i] - 20);
        lv_obj_set_style_bg_color(crosshairs[i], lv_color_hex(0x0f3460), 0);
        lv_obj_set_style_border_width(crosshairs[i], 2, 0);
        lv_obj_set_style_border_color(crosshairs[i], lv_color_hex(0x555555), 0);
        lv_obj_set_style_radius(crosshairs[i], 20, 0);
        lv_obj_clear_flag(crosshairs[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *cross_label = lv_label_create(crosshairs[i]);
        char point_buf[8];
        snprintf(point_buf, sizeof(point_buf), "%d", i + 1);
        lv_label_set_text(cross_label, point_buf);
        lv_obj_set_style_text_color(cross_label, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(cross_label, &lv_font_montserrat_16, 0);
        lv_obj_center(cross_label);
    }

    lv_obj_add_event_cb(scr, touch_calibrate_cb, LV_EVENT_PRESSED, NULL);

    return scr;
}

void calibration_screen_set_done_cb(calibration_done_cb_t cb)
{
    done_cb = cb;
}

void calibration_screen_set_back_scr(lv_obj_t *scr)
{
    back_scr = scr;
}
