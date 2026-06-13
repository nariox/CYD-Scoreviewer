#include "splash_screen.h"
#include "basketball_img.h"
#include <stdio.h>

static void set_angle(void *obj, int32_t v)
{
    lv_arc_set_value(obj, v);
}

lv_obj_t *splash_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);

    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &basketball_img);
    lv_obj_set_pos(img, 16, 88);

    lv_obj_t *name_label = lv_label_create(scr);
    lv_label_set_text(name_label, "Sami's basketball");
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(name_label, 92, 98);

    lv_obj_t *subtitle_label = lv_label_create(scr);
    lv_label_set_text(subtitle_label, "score viewer");
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(subtitle_label, 92, 122);

    lv_obj_t *spinner = lv_arc_create(scr);
    lv_arc_set_rotation(spinner, 270);
    lv_arc_set_bg_angles(spinner, 0, 360);
    lv_obj_remove_style(spinner, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(spinner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0xe94560), LV_PART_INDICATOR);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, spinner);
    lv_anim_set_exec_cb(&a, set_angle);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, 100);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_start(&a);

    return scr;
}
