#include "settings_screen.h"
#include "wifi_screen.h"
#include "lvgl.h"
#include "esp_log.h"
#include "driver/ledc.h"

static const char *TAG = "settings";

static lv_obj_t *wifi_scr = NULL;
static lv_obj_t *cards_scr = NULL;
static lv_obj_t *calibration_scr = NULL;
static lv_obj_t *brightness_slider;

static void wifi_btn_event_cb(lv_event_t *e)
{
    if (wifi_scr) {
        lv_scr_load(wifi_scr);
    }
}

static void nav_to_cards(lv_event_t *e)
{
    if (cards_scr) {
        lv_scr_load(cards_scr);
    }
}

static void brightness_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, 0, value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, 0);
}

static void calibration_btn_event_cb(lv_event_t *e)
{
    if (calibration_scr) {
        lv_scr_load(calibration_scr);
    }
}

void settings_screen_set_wifi_scr(lv_obj_t *scr)
{
    wifi_scr = scr;
}

void settings_screen_set_cards_scr(lv_obj_t *scr)
{
    cards_scr = scr;
}

void settings_screen_set_calibration_scr(lv_obj_t *scr)
{
    calibration_scr = scr;
}

lv_obj_t *settings_screen_create(void)
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

    lv_obj_add_event_cb(back_btn, nav_to_cards, LV_EVENT_CLICKED, NULL);

    lv_obj_t *header_title = lv_label_create(header);
    lv_label_set_text(header_title, "Settings");
    lv_obj_set_style_text_color(header_title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(header_title, &lv_font_montserrat_16, 0);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100) - 40);
    lv_obj_set_pos(content, 0, 40);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);

    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_set_style_pad_left(content, 20, 0);
    lv_obj_set_style_pad_right(content, 20, 0);

    lv_obj_t *brightness_label = lv_label_create(content);
    lv_label_set_text(brightness_label, "Brightness");
    lv_obj_set_style_text_color(brightness_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_16, 0);
    lv_obj_set_width(brightness_label, LV_PCT(100));
    lv_obj_set_style_text_align(brightness_label, LV_TEXT_ALIGN_CENTER, 0);

    brightness_slider = lv_slider_create(content);
    lv_obj_set_size(brightness_slider, 200, 15);
    lv_obj_center(brightness_slider);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x333333), LV_PART_KNOB);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0xffffff), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(brightness_slider, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(brightness_slider, 0, 0);
    lv_obj_remove_flag(brightness_slider, LV_OBJ_FLAG_SCROLLABLE);
    lv_slider_set_range(brightness_slider, 0, 255);
    lv_slider_set_value(brightness_slider, 128, LV_ANIM_OFF);

    lv_obj_add_event_cb(brightness_slider, brightness_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *wifi_btn_container = lv_obj_create(content);
    lv_obj_set_size(wifi_btn_container, LV_PCT(100), 45);
    lv_obj_set_flex_flow(wifi_btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_btn_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(wifi_btn_container, 0, 0);
    lv_obj_set_style_bg_opa(wifi_btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_btn_container, 0, 0);

    lv_obj_t *wifi_btn = lv_btn_create(wifi_btn_container);
    lv_obj_set_size(wifi_btn, 36, 36);
    lv_obj_set_style_bg_color(wifi_btn, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_btn, 1, 0);
    lv_obj_set_style_border_color(wifi_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(wifi_btn, 8, 0);
    lv_obj_clear_flag(wifi_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_16, 0);
    lv_obj_center(wifi_label);

    lv_obj_t *wifi_text = lv_label_create(wifi_btn_container);
    lv_label_set_text(wifi_text, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_text, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(wifi_text, &lv_font_montserrat_16, 0);

    lv_obj_add_event_cb(wifi_btn, wifi_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *calibration_btn_container = lv_obj_create(content);
    lv_obj_set_size(calibration_btn_container, LV_PCT(100), 45);
    lv_obj_set_flex_flow(calibration_btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(calibration_btn_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(calibration_btn_container, 0, 0);
    lv_obj_set_style_bg_opa(calibration_btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(calibration_btn_container, 0, 0);

    lv_obj_t *calibration_btn = lv_btn_create(calibration_btn_container);
    lv_obj_set_size(calibration_btn, 36, 36);
    lv_obj_set_style_bg_color(calibration_btn, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_border_width(calibration_btn, 1, 0);
    lv_obj_set_style_border_color(calibration_btn, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(calibration_btn, 8, 0);
    lv_obj_clear_flag(calibration_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *calibration_label = lv_label_create(calibration_btn);
    lv_label_set_text(calibration_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(calibration_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(calibration_label, &lv_font_montserrat_16, 0);
    lv_obj_center(calibration_label);

    lv_obj_t *calibration_text = lv_label_create(calibration_btn_container);
    lv_label_set_text(calibration_text, "Touch Calibration");
    lv_obj_set_style_text_color(calibration_text, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(calibration_text, &lv_font_montserrat_16, 0);

    lv_obj_add_event_cb(calibration_btn, calibration_btn_event_cb, LV_EVENT_CLICKED, NULL);

    return scr;
}
