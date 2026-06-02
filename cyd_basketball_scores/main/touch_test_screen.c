#include "touch_test_screen.h"
#include <stdio.h>
#include "lvgl.h"
#include "esp_log.h"
#include "touch_integration.h"

static const char *TAG = "touch_test";

static lv_obj_t *coords_label;
static lv_obj_t *button_labels[25];
static lv_obj_t * cursor_obj;

static void button_pressed_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_label_set_text(lbl, "PRESSED");
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x00b4d8), LV_PART_MAIN);
    }
    uint32_t tick = lv_tick_get();
    ESP_LOGI(TAG, "Button pressed at %lu ms", tick);
}

static void update_labels_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED) {
        lv_indev_t * indev = lv_indev_get_act();
        lv_point_t point;
        lv_indev_get_point(indev, &point);

        char buf[64];
        snprintf(buf, sizeof(buf), "X: %ld  Y: %ld", point.x, point.y);
        lv_label_set_text(coords_label, buf);
        lv_obj_set_pos(cursor_obj, point.x, point.y);
    }
}

lv_obj_t *touch_test_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x16213e), 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);

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
    lv_obj_clear_flag(header, LV_OBJ_FLAG_CLICKABLE); // Allow is to be transparent to clicks

    lv_obj_t *header_title = lv_label_create(header);
    lv_label_set_text(header_title, "Touch Test");
    lv_obj_set_style_text_color(header_title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(header_title, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(header_title, 120, 6);

    lv_obj_t *info_label = lv_label_create(header);
    lv_label_set_text(info_label, "Tap any button");
    lv_obj_set_style_text_color(info_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(info_label, 10, 16);

    coords_label = lv_label_create(header);
    lv_label_set_text(coords_label, "X: --- Y: ---");
    lv_obj_set_style_text_color(coords_label, lv_color_hex(0x00b4d8), 0);
    lv_obj_set_style_text_font(coords_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(coords_label, 200, 16);
    lv_obj_add_event_cb(scr, update_labels_cb, LV_EVENT_CLICKED, NULL);

    char *btn_texts[] = {"TL", "TCL", "TC", "TC,R", "TR",
                         "MTL", "MTCL", "MTC", "MTC,R", "MTR",
                         "ML", "MCL", "C",     "MCR", "MR",
                         "MBL", "MBCL", "MBC", "MBCR", "MBR",
                         "BL", "BCL", "BC", "BCR", "BR"};

    int positions[][2] = {{32,  60}, {96,  60}, {160,  60}, {224,  60}, {288,  60},
                          {32, 100}, {96, 100}, {160, 100}, {224, 100}, {288, 100},
                          {32, 140}, {96, 140}, {160, 140}, {224, 140}, {288, 140},
                          {32, 180}, {96, 180}, {160, 180}, {224, 180}, {288, 180},
                          {32, 220}, {96, 220}, {160, 220}, {224, 220}, {288, 220}};

    for (int i = 0; i < 25; i++) {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 60, 30);
        lv_obj_set_pos(btn, positions[i][0]-30, positions[i][1]-15);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x555555), 0);
        lv_obj_set_style_radius(btn, 2, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);

        button_labels[i] = lv_label_create(btn);
        lv_label_set_text(button_labels[i], btn_texts[i]);
        lv_obj_set_style_text_color(button_labels[i], lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(button_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_center(button_labels[i]);

        lv_obj_add_event_cb(btn, button_pressed_cb, LV_EVENT_PRESSED, NULL);
    }

    // Crosshair
    cursor_obj = lv_obj_create(scr); // Create
    lv_obj_set_size(cursor_obj, 7, 7); // Size 9
    lv_obj_set_style_radius(cursor_obj, LV_RADIUS_CIRCLE, 0); // Make it round
    lv_obj_set_style_bg_color(cursor_obj, lv_color_hex(0xFFFFFF), 0); // Make it black
    lv_obj_set_style_border_width(cursor_obj, 0, 0); // No borders
    lv_obj_clear_flag(cursor_obj, LV_OBJ_FLAG_CLICKABLE); // Allow is to be transparent to clicks

    return scr;
}

lv_obj_t *touch_test_screen_get_coords_label(void)
{
    return coords_label;
}
