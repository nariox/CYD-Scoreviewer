#include "touch_test_screen.h"
#include <stdio.h>
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "touch_test";

lv_obj_t *coords_label;
static lv_obj_t *button_labels[9];

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

lv_obj_t *touch_test_screen_create(void)
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

    char *btn_texts[] = {"Top-Left", "Top-Center", "Top-Right",
                         "Mid-Left", "Center", "Mid-Right",
                         "Bot-Left", "Bot-Center", "Bot-Right"};

    int positions[][2] = {{40, 70}, {140, 70}, {240, 70},
                          {40, 130}, {140, 130}, {240, 130},
                          {40, 190}, {140, 190}, {240, 190}};

    for (int i = 0; i < 9; i++) {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 70, 40);
        lv_obj_set_pos(btn, positions[i][0], positions[i][1]);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x555555), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        button_labels[i] = lv_label_create(btn);
        lv_label_set_text(button_labels[i], btn_texts[i]);
        lv_obj_set_style_text_color(button_labels[i], lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(button_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_center(button_labels[i]);

        lv_obj_add_event_cb(btn, button_pressed_cb, LV_EVENT_PRESSED, NULL);
    }

    return scr;
}
