#include "wifi_screen.h"
#include "settings_screen.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "wifi";

static lv_obj_t *settings_scr = NULL;
static lv_obj_t *cards_scr = NULL;

static void back_btn_event_cb(lv_event_t *e)
{
    if (settings_scr) {
        lv_scr_load(settings_scr);
    }
}

static void nav_to_cards(lv_event_t *e)
{
    if (cards_scr) {
        lv_scr_load(cards_scr);
    }
}

lv_obj_t *wifi_screen_create(void)
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
    lv_label_set_text(header_title, "Wi-Fi");
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

    lv_obj_t *wifi_label = lv_label_create(content);
    lv_label_set_text(wifi_label, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_16, 0);

    lv_obj_t *wifi_switch = lv_switch_create(content);
    lv_obj_set_size(wifi_switch, 50, 30);
    lv_obj_set_style_bg_color(wifi_switch, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_bg_color(wifi_switch, lv_color_hex(0xffffff), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(wifi_switch, lv_color_hex(0x00b4d8), LV_PART_KNOB);

    lv_obj_t *network_label = lv_label_create(content);
    lv_label_set_text(network_label, "Networks");
    lv_obj_set_style_text_color(network_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(network_label, &lv_font_montserrat_16, 0);

    lv_obj_t *network_list = lv_obj_create(content);
    lv_obj_set_size(network_list, LV_PCT(100), 60);
    lv_obj_set_style_bg_color(network_list, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(network_list, 1, 0);
    lv_obj_set_style_border_color(network_list, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(network_list, 8, 0);

    lv_obj_t *placeholder = lv_label_create(network_list);
    lv_label_set_text(placeholder, "Scan for networks...");
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_16, 0);
    lv_obj_center(placeholder);

    lv_obj_t *ip_label = lv_label_create(content);
    lv_label_set_text(ip_label, "IP Config");
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_16, 0);

    lv_obj_t *ip_dropdown = lv_dropdown_create(content);
    lv_obj_set_size(ip_dropdown, 120, 35);
    lv_dropdown_set_options(ip_dropdown, "DHCP\nManual");
    lv_obj_set_style_bg_color(ip_dropdown, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ip_dropdown, lv_color_hex(0xffffff), LV_PART_SELECTED);
    lv_obj_set_style_text_color(ip_dropdown, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(ip_dropdown, 1, 0);
    lv_obj_set_style_border_color(ip_dropdown, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(ip_dropdown, 6, 0);

    return scr;
}

void wifi_screen_set_settings_scr(lv_obj_t *scr)
{
    settings_scr = scr;
}

void wifi_screen_set_cards_scr(lv_obj_t *scr)
{
    cards_scr = scr;
}
