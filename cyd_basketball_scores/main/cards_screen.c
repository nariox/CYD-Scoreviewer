#include "cards_screen.h"
#include <stdio.h>

lv_obj_t *cards_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x16213e), 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header_label = lv_label_create(header);
    lv_label_set_text(header_label, "NBA SCORES");
    lv_obj_set_style_text_color(header_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(header_label, &lv_font_montserrat_20, 0);
    lv_obj_align(header_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *cards_container = lv_obj_create(scr);
    lv_obj_set_size(cards_container, LV_PCT(100), LV_PCT(100) - 50);
    lv_obj_set_pos(cards_container, 0, 50);
    lv_obj_set_style_bg_color(cards_container, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_width(cards_container, 0, 0);
    lv_obj_set_style_radius(cards_container, 0, 0);
    lv_obj_set_flex_flow(cards_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(cards_container, LV_DIR_VER);

    lv_obj_t *placeholder = lv_label_create(cards_container);
    lv_label_set_text(placeholder, "No games to display");
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_16, 0);
    lv_obj_center(placeholder);

    return scr;
}
