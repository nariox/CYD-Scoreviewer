/* ui_common.h — shared UI dimensions and colors for all screens (320x240 CYD).
 * Keep these here instead of magic numbers in the screen files. */
#pragma once

#include "lvgl.h"

/* ---- Colors (0xRRGGBB, use with lv_color_hex()) ---- */
#define UI_COL_BG           0x16213e
#define UI_COL_HEADER       0x0f3460
#define UI_COL_TEXT         0xffffff
#define UI_COL_PLACEHOLDER  0x888888  /* also used for muted/secondary text */
#define UI_COL_SUBTLE       0x555555
#define UI_COL_ACCENT       0x00b4d8
#define UI_COL_DANGER       0xff4444
#define UI_COL_WARN         0xffaa00
#define UI_COL_OK           0x00ff00
#define UI_COL_SHADOW       0x000000

/* ---- Screen structure ---- */
#define UI_HEADER_H         40   /* header bar (wifi/settings/calibration) */
#define UI_HEADER_H_CARDS   50   /* cards header (larger font) */
#define UI_CONTENT_H        (LV_VER_RES - UI_HEADER_H)
#define UI_KEYBOARD_H       110  /* on-screen keyboard height */

/* ---- Header widgets ---- */
#define UI_BTN_SM           28   /* small square buttons (back, connect) */
#define UI_BTN_MD           36   /* medium square buttons (gear, menu rows) */

/* ---- Centered form column (wifi screen) ---- */
#define UI_ROW_W            220  /* width of every centered row */
#define UI_ROW_H            22   /* one list row inside a list box */
#define UI_BTN_H            26   /* wide button (Scan) */
#define UI_TA_H             34   /* text area */
#define UI_LIST_H           44   /* scrolling list box */
#define UI_ACTIONBAR_H      36   /* connect/disconnect bar */
#define UI_DEL_BTN          20   /* 'x' delete button in saved rows */
#define UI_FORM_GAP         6    /* pad_row between form rows */
#define UI_FORM_PAD_X       12   /* content side padding */
#define UI_FORM_PAD_TOP     4    /* content top padding */

/* ---- Settings menu rows ---- */
#define UI_MENU_ROW_H       45
#define UI_MENU_ROW_GAP     12
#define UI_MENU_PAD_X       20
#define UI_SLIDER_W         200

/* ---- Calibration info boxes ---- */
#define UI_INFOBOX_W        140
#define UI_INFOBOX_H        25
