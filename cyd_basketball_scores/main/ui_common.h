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
#define UI_COL_PRESSED      0x1b4b7a  /* pressed card/button shade (lighter header) */
#define UI_COL_SHADOW       0x000000

/* ---- Screen structure ---- */
#define UI_HEADER_H         40   /* header bar (wifi/settings/calibration) */
#define UI_HEADER_H_CARDS   50   /* cards header (larger font) */
#define UI_PAGEBAR_H        26   /* page pager strip under the cards header */
#define UI_PAGEBTN_W        30   /* [<<] / [>>] buttons in the pager */
#define UI_PAGEBAR_DATE_W   150  /* date range label in the pager */
#define UI_CONTENT_H        (LV_VER_RES - UI_HEADER_H)
#define UI_KEYBOARD_H       110  /* on-screen keyboard height */

/* ---- Header widgets ---- */
#define UI_BTN_SM           28   /* small square buttons (back, connect) */
#define UI_BTN_MD           36   /* medium square buttons (gear, menu rows) */
#define UI_TAB_W            33   /* league selector labels (NBA/WNBA/MBB/WBB):
                                    4 tabs must fit next to wifi+buttons */
#define UI_TAB_H            24
#define UI_TAB_GAP          4

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

/* ---- Score cards (cards screen) ---- */
#define UI_CARD_W           300  /* score card width (320 - 2 x 10 side margin) */
#define UI_CARD_H           46   /* one score row box: score line (top-aligned
                                    with the date) + team names at the bottom */
#define UI_CARD_GAP         8    /* vertical gap between cards */
#define UI_CARD_PAD_X       10   /* card inner horizontal padding */
#define UI_CARD_COL_GAP     8    /* gap between team / score / team labels */
#define UI_CARD_DATE_PAD_X  8    /* corner label inset from card side edge */
#define UI_CARD_DATE_PAD_Y  3    /* corner label inset from card top edge */
#define UI_CARD_NAME_W      112  /* team name along the bottom; the middle gap
                                    between them carries the live game clock */
#define UI_CARD_NAME_H      13   /* one 12pt line: longer names clip with dots */
#define UI_CARD_NAME_ROW_Y  28   /* y of the name/clock band under the row
                                    (card top pad 3 + row 26 -> names at 31) */
#define UI_CARD_CENTER_W    100  /* cap for the score/tip text on the row:
                                    keeps the widest card clear of the date */
#define UI_CARD_CENTER_H    26   /* LONG_DOT needs a bounded height */
#define UI_CARD_NAME_PAD_Y  2    /* name label inset from card bottom edge */

/* ---- Game detail screen (tap a card) ---- */
#define UI_DET_HEAD_H       UI_HEADER_H_CARDS /* same header band as the cards screen */
#define UI_DET_SCORE_Y      34   /* big score line, below the header */
#define UI_DET_SCORE_H      26   /* score line row (also holds the team logos) */
#define UI_DET_NAME_W       118  /* team name column of the box-score table */
#define UI_DET_NAME_H       18   /* one 12pt line: longer names get clipped with dots */
#define UI_DET_COL_W        29   /* Q1..Q4 / OT column */
#define UI_DET_TOTAL_W      36   /* total column */
#define UI_DET_ROW_H        24   /* one box-score table row */
