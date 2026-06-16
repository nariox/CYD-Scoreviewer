#include "wifi_screen.h"
#include "wifi_backend.h"
#include "nvs_settings.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_screen";

static lv_obj_t *settings_scr = NULL;
static lv_obj_t *cards_scr = NULL;

static lv_obj_t *status_label = NULL;
static lv_obj_t *ssid_ta = NULL;
static lv_obj_t *pwd_ta = NULL;
static lv_obj_t *keyboard = NULL;
static lv_obj_t *scan_list = NULL;
static lv_obj_t *saved_list = NULL;
static lv_obj_t *connect_btn = NULL;
static lv_obj_t *scan_btn = NULL;

static wifi_ap_entry_t s_scan_cache[WIFI_BACKEND_MAX_AP];
static uint16_t s_scan_cache_count = 0;

static void scan_callback(wifi_scan_results_t *results, void *user_data);
static void apply_scan_results_timer(lv_timer_t *timer);
static void build_saved_networks(void);
static void defer_build_saved_networks(lv_timer_t *timer);

static const char *state_to_string(wifi_state_t state)
{
    switch (state) {
        case WIFI_STATE_IDLE:         return "";
        case WIFI_STATE_INITIALIZING: return "Initializing...";
        case WIFI_STATE_SCANNING:     return "Scanning...";
        case WIFI_STATE_CONNECTING:   return "Connecting...";
        case WIFI_STATE_CONNECTED:    return "Connected";
        case WIFI_STATE_FAILED:       return "Connection failed";
        case WIFI_STATE_DISCONNECTED: return "Disconnected";
        default:                      return "";
    }
}

static void style_btn(lv_obj_t *btn)
{
    lv_obj_set_style_shadow_width(btn, 8, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 2, 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
}

static void back_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (settings_scr) {
        lv_scr_load(settings_scr);
    }
}

static void nav_to_cards(lv_event_t *e)
{
    (void)e;
    if (cards_scr) {
        lv_scr_load(cards_scr);
    }
}

static void textarea_focus_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(keyboard, ta);
        lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, NULL);
    }
}

static void scan_btn_event_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status_label, "Scanning...");
    lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
    lv_obj_add_state(connect_btn, LV_STATE_DISABLED);
    wifi_backend_scan(scan_callback, NULL);
}

static void ssid_btn_click_cb(lv_event_t *e)
{
    uint16_t idx = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx < s_scan_cache_count) {
        lv_textarea_set_text(ssid_ta, s_scan_cache[idx].ssid);
        lv_obj_add_state(pwd_ta, LV_STATE_FOCUSED);
        lv_keyboard_set_textarea(keyboard, pwd_ta);
        lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void saved_net_click_cb(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    nvs_wifi_credentials_t creds;
    memset(&creds, 0, sizeof(creds));
    nvs_settings_load_wifi_networks(&creds);
    if (idx < creds.count && strlen(creds.networks[idx].ssid) > 0) {
        lv_textarea_set_text(ssid_ta, creds.networks[idx].ssid);
        lv_textarea_set_text(pwd_ta, creds.networks[idx].password);
    }
}

static void saved_net_delete_cb(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    nvs_settings_delete_wifi_network(idx);
    wifi_screen_refresh_saved_networks();
}

static void connect_btn_event_cb(lv_event_t *e)
{
    (void)e;
    const char *ssid = lv_textarea_get_text(ssid_ta);
    const char *pwd = lv_textarea_get_text(pwd_ta);

    if (strlen(ssid) == 0) {
        lv_label_set_text(status_label, "Enter SSID");
        return;
    }

    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
    lv_obj_add_state(connect_btn, LV_STATE_DISABLED);
    wifi_backend_connect(ssid, strlen(pwd) > 0 ? pwd : "");
}

static void status_update_cb(lv_timer_t *timer)
{
    (void)timer;
    wifi_state_t state = wifi_backend_get_state();
    const char *state_str = state_to_string(state);

    char buf[64] = "";
    if (state == WIFI_STATE_CONNECTED) {
        snprintf(buf, sizeof(buf), "Connected: %s (%s)",
                 wifi_backend_get_connected_ssid(),
                 wifi_backend_get_ip());
    } else {
        strncpy(buf, state_str, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }

    lv_label_set_text(status_label, buf);

    lv_color_t label_color;
    switch (state) {
        case WIFI_STATE_CONNECTED:
            label_color = lv_color_hex(0x00ff00);
            lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
            lv_obj_clear_state(connect_btn, LV_STATE_DISABLED);
            break;
        case WIFI_STATE_FAILED:
            label_color = lv_color_hex(0xff4444);
            lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
            lv_obj_clear_state(connect_btn, LV_STATE_DISABLED);
            break;
        case WIFI_STATE_CONNECTING:
            label_color = lv_color_hex(0xffaa00);
            break;
        case WIFI_STATE_SCANNING:
            label_color = lv_color_hex(0xffaa00);
            break;
        default:
            label_color = lv_color_hex(0xffffff);
            lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
            lv_obj_clear_state(connect_btn, LV_STATE_DISABLED);
            break;
    }
    lv_obj_set_style_text_color(status_label, label_color, 0);
}

static void scan_callback(wifi_scan_results_t *results, void *user_data)
{
    (void)user_data;
    s_scan_cache_count = results->count;
    if (s_scan_cache_count > WIFI_BACKEND_MAX_AP) {
        s_scan_cache_count = WIFI_BACKEND_MAX_AP;
    }
    memcpy(s_scan_cache, results->entries,
           sizeof(wifi_ap_entry_t) * s_scan_cache_count);

    lv_timer_t *t = lv_timer_create(apply_scan_results_timer, 0, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void apply_scan_results_timer(lv_timer_t *timer)
{
    (void)timer;
    while (lv_obj_get_child_cnt(scan_list) > 0) {
        lv_obj_delete(lv_obj_get_child(scan_list, 0));
    }

    if (s_scan_cache_count == 0) {
        lv_obj_t *no_label = lv_label_create(scan_list);
        lv_label_set_text(no_label, "No networks found");
        lv_obj_set_style_text_color(no_label, lv_color_hex(0x555555), 0);
        lv_obj_set_style_text_font(no_label, &lv_font_montserrat_12, 0);
        lv_obj_center(no_label);
    } else {
        for (uint16_t i = 0; i < s_scan_cache_count; i++) {
            lv_obj_t *row = lv_obj_create(scan_list);
            lv_obj_set_size(row, LV_PCT(100), 22);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);

            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t *ssid_lbl = lv_label_create(row);
            char ssid_buf[36];
            snprintf(ssid_buf, sizeof(ssid_buf), "%s", s_scan_cache[i].ssid);
            lv_label_set_text(ssid_lbl, ssid_buf);
            lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_max_width(ssid_lbl, 180, 0);

            lv_obj_t *rssi_lbl = lv_label_create(row);
            char rssi_buf[12];
            snprintf(rssi_buf, sizeof(rssi_buf), "%ddBm", s_scan_cache[i].rssi);
            lv_label_set_text(rssi_lbl, rssi_buf);
            lv_obj_set_style_text_color(rssi_lbl, lv_color_hex(0x888888), 0);
            lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_pad_left(rssi_lbl, 8, 0);

            lv_obj_add_event_cb(row, ssid_btn_click_cb, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
        }
    }

    lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
    lv_obj_clear_state(connect_btn, LV_STATE_DISABLED);
    lv_label_set_text(status_label, "Scan complete");
}

static void build_saved_networks(void)
{
    if (!saved_list) {
        return;
    }

    while (lv_obj_get_child_cnt(saved_list) > 0) {
        lv_obj_delete(lv_obj_get_child(saved_list, 0));
    }

    nvs_wifi_credentials_t creds;
    memset(&creds, 0, sizeof(creds));
    nvs_settings_load_wifi_networks(&creds);

    if (creds.count == 0) {
        lv_obj_t *no_label = lv_label_create(saved_list);
        lv_label_set_text(no_label, "No saved networks");
        lv_obj_set_style_text_color(no_label, lv_color_hex(0x555555), 0);
        lv_obj_set_style_text_font(no_label, &lv_font_montserrat_12, 0);
        lv_obj_center(no_label);
        return;
    }

    for (uint8_t i = 0; i < creds.count; i++) {
        lv_obj_t *row = lv_obj_create(saved_list);
        lv_obj_set_size(row, LV_PCT(100), 22);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);

        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END);

        lv_obj_t *ssid_lbl = lv_label_create(row);
        lv_label_set_text(ssid_lbl, creds.networks[i].ssid);
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0x00b4d8), 0);
        lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_12, 0);

        lv_obj_t *del_btn = lv_btn_create(row);
        lv_obj_set_size(del_btn, 20, 20);
        lv_obj_set_style_bg_color(del_btn, lv_color_hex(0x0f3460), LV_PART_MAIN);
        lv_obj_set_style_border_width(del_btn, 0, 0);
        lv_obj_set_style_radius(del_btn, 4, 0);

        lv_obj_t *del_lbl = lv_label_create(del_btn);
        lv_label_set_text(del_lbl, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(del_lbl, lv_color_hex(0xff4444), 0);
        lv_obj_set_style_text_font(del_lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(del_lbl);

        lv_obj_add_event_cb(row, saved_net_click_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_obj_add_event_cb(del_btn, saved_net_delete_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }
}

static void defer_build_saved_networks(lv_timer_t *timer)
{
    (void)timer;
    build_saved_networks();
}

lv_obj_t *wifi_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Header */
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
    style_btn(back_btn);

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

    /* Content area */
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100) - 40);
    lv_obj_set_pos(content, 0, 40);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);

    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 6, 0);
    lv_obj_set_style_pad_left(content, 12, 0);
    lv_obj_set_style_pad_right(content, 12, 0);
    lv_obj_set_style_pad_top(content, 4, 0);

    /* Status label */
    status_label = lv_label_create(content);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

    /* Scan button */
    scan_btn = lv_btn_create(content);
    lv_obj_set_size(scan_btn, LV_PCT(100), 26);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_radius(scan_btn, 6, 0);
    style_btn(scan_btn);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "Scan Networks");
    lv_obj_set_style_text_color(scan_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(scan_lbl);
    lv_obj_add_event_cb(scan_btn, scan_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* Saved networks section */
    lv_obj_t *saved_header = lv_label_create(content);
    lv_label_set_text(saved_header, "Saved:");
    lv_obj_set_style_text_color(saved_header, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(saved_header, &lv_font_montserrat_12, 0);

    saved_list = lv_obj_create(content);
    lv_obj_set_size(saved_list, LV_PCT(100), 44);
    lv_obj_set_style_bg_opa(saved_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(saved_list, 0, 0);
    lv_obj_set_style_radius(saved_list, 0, 0);
    lv_obj_set_flex_flow(saved_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(saved_list, 2, 0);
    lv_obj_set_scroll_dir(saved_list, LV_DIR_VER);
    lv_timer_t *defer_t = lv_timer_create(defer_build_saved_networks, 10, NULL);
    lv_timer_set_repeat_count(defer_t, 1);

    /* SSID textarea */
    ssid_ta = lv_textarea_create(content);
    lv_textarea_set_one_line(ssid_ta, true);
    lv_textarea_set_placeholder_text(ssid_ta, "SSID");
    lv_textarea_set_max_length(ssid_ta, 32);
    lv_obj_set_size(ssid_ta, LV_PCT(100), 26);
    lv_obj_set_style_bg_color(ssid_ta, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_text_color(ssid_ta, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_color(ssid_ta, lv_color_hex(0x888888), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(ssid_ta, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(ssid_ta, 1, 0);
    lv_obj_set_style_radius(ssid_ta, 6, 0);
    lv_obj_set_style_text_font(ssid_ta, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(ssid_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ssid_ta, textarea_focus_cb, LV_EVENT_ALL, NULL);

    /* Password textarea */
    pwd_ta = lv_textarea_create(content);
    lv_textarea_set_one_line(pwd_ta, true);
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_textarea_set_placeholder_text(pwd_ta, "Password");
    lv_textarea_set_max_length(pwd_ta, 63);
    lv_obj_set_size(pwd_ta, LV_PCT(100), 26);
    lv_obj_set_style_bg_color(pwd_ta, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_text_color(pwd_ta, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_color(pwd_ta, lv_color_hex(0x888888), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(pwd_ta, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(pwd_ta, 1, 0);
    lv_obj_set_style_radius(pwd_ta, 6, 0);
    lv_obj_set_style_text_font(pwd_ta, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(pwd_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pwd_ta, textarea_focus_cb, LV_EVENT_ALL, NULL);

    /* Scan results list */
    scan_list = lv_obj_create(content);
    lv_obj_set_size(scan_list, LV_PCT(100), 50);
    lv_obj_set_style_bg_color(scan_list, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(scan_list, 1, 0);
    lv_obj_set_style_border_color(scan_list, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(scan_list, 6, 0);
    lv_obj_set_flex_flow(scan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scan_list, 1, 0);
    lv_obj_set_scroll_dir(scan_list, LV_DIR_VER);

    lv_obj_t *placeholder = lv_label_create(scan_list);
    lv_label_set_text(placeholder, "Tap Scan to find networks");
    lv_obj_set_style_text_color(placeholder, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(placeholder, &lv_font_montserrat_12, 0);
    lv_obj_center(placeholder);

    /* Connect button */
    connect_btn = lv_btn_create(content);
    lv_obj_set_size(connect_btn, LV_PCT(60), 28);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x00b4d8), LV_PART_MAIN);
    lv_obj_set_style_radius(connect_btn, 6, 0);
    style_btn(connect_btn);
    lv_obj_align(connect_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *conn_lbl = lv_label_create(connect_btn);
    lv_label_set_text(conn_lbl, "Connect");
    lv_obj_set_style_text_color(conn_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(conn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(conn_lbl);
    lv_obj_add_event_cb(connect_btn, connect_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* Keyboard (hidden by default) */
    keyboard = lv_keyboard_create(scr);
    lv_obj_set_size(keyboard, LV_HOR_RES, 110);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);

    /* Status update timer (polls backend state every 500ms) */
    lv_timer_create(status_update_cb, 500, NULL);

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

void wifi_screen_refresh_saved_networks(void)
{
    if (saved_list) {
        build_saved_networks();
    }
}
