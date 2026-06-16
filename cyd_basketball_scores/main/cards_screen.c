#include "cards_screen.h"
#include "settings_screen.h"
#include "lvgl.h"

static lv_obj_t *settings_scr = NULL;
static lv_obj_t *wifi_status_label = NULL;

static void nav_to_settings(lv_event_t *e)
{
    lv_obj_t *target = (lv_obj_t *)lv_event_get_user_data(e);
    lv_scr_load(target);
}

static void settings_btn_event_cb(lv_event_t *e)
{
    if (settings_scr) {
        lv_scr_load(settings_scr);
    }
}

void cards_screen_set_settings_scr(lv_obj_t *scr)
{
    settings_scr = scr;
}

void cards_screen_update_wifi_status(wifi_state_t state)
{
    if (!wifi_status_label) return;

    const char *text;
    lv_color_t color;

    switch (state) {
        case WIFI_STATE_CONNECTED:
            text = "Connected";
            color = lv_color_hex(0x00ff00);
            break;
        case WIFI_STATE_CONNECTING:
        case WIFI_STATE_SCANNING:
            text = "Connecting...";
            color = lv_color_hex(0xffaa00);
            break;
        case WIFI_STATE_FAILED:
        case WIFI_STATE_DISCONNECTED:
            text = "Disconnected";
            color = lv_color_hex(0xff4444);
            break;
        default:
            text = "";
            color = lv_color_hex(0x555555);
            break;
    }

    lv_label_set_text(wifi_status_label, text);
    lv_obj_set_style_text_color(wifi_status_label, color, 0);
}

lv_obj_t *cards_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, LV_PCT(100), 50);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *header_label = lv_label_create(header);
    lv_label_set_text(header_label, "NBA SCORES");
    lv_obj_set_style_text_color(header_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(header_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_left(header_label, 16, 0);

    wifi_status_label = lv_label_create(header);
    lv_label_set_text(wifi_status_label, "");
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_pad_left(wifi_status_label, 8, 0);

    lv_obj_t *settings_btn = lv_btn_create(header);
    lv_obj_set_size(settings_btn, 36, 36);
    lv_obj_set_style_bg_color(settings_btn, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_btn, 0, 0);
    lv_obj_set_style_radius(settings_btn, 8, 0);
    lv_obj_clear_flag(settings_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(settings_btn, 8, 0);
    lv_obj_set_style_shadow_ofs_y(settings_btn, 2, 0);
    lv_obj_set_style_shadow_opa(settings_btn, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(settings_btn, lv_color_hex(0x000000), 0);

    lv_obj_t *settings_label = lv_label_create(settings_btn);
    lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(settings_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(settings_label, &lv_font_montserrat_16, 0);
    lv_obj_center(settings_label);

    lv_obj_add_event_cb(settings_btn, settings_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_right(settings_btn, 16, 0);

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
