# CYD Basketball Scores

LVGL 9 + ESP-IDF basketball score viewer for the **CYD2USB** (ESP32-2432S028, dual USB port variant).

## Hardware

- **Board**: ESP32-2432S028 (CYD2USB, no "R")
- **Display**: 320x240 ST7789 (SPI, HSPI bus)
- **Touch**: XPT2046 resistive touch (SPI3, integrated with LVGL indev)
- **Pinout**:

| Signal | GPIO |
|--------|------|
| LCD MOSI | 13 |
| LCD MISO | 12 |
| LCD SCLK | 14 |
| LCD CS   | 15 |
| LCD DC   | 2 |
| LCD RST  | 4 |
| LCD BL   | 21 |
| Touch CS | 33 |
| Touch IRQ | 36 |

## Layout cheat sheet (320x240)

| Band | Height |
|------|--------|
| Header (`NBA SCORES` + Wi-Fi status + refresh + gear) | 50 px (`UI_HEADER_H_CARDS`) |
| Date pager (`<  Oct 15 …… 4/8  >`) | 26 px (`UI_PAGEBAR_H`) |
| Card list | 164 px (`LV_VER_RES - UI_HEADER_H_CARDS - UI_PAGEBAR_H`) |

Card list: flex column, 300 x 44 px cards, 8 px row gap (`UI_CARD_*`) — ~3 cards visible, the rest scrolls.

Pager: a page holds a fixed number of games (8 by default, `CYD_NBA_PAGE_SIZE`), newest first — a busy slate spills over several pages, a quiet stretch pulls several days into one, and the strip shows the date range it covers. `<` loads older games, `>` goes back to newer ones. The feed keeps the last 4 pages in RAM, so paging inside that window is instant and free; stepping past it fetches one more chunk of days. The arrow at the end of the range is greyed out.

Card anatomy: `TEAM_A  102 - 98  TEAM_B` centred (teams 16pt, score 20pt), game date in 12pt at the top-right, game clock / state (`Q2 05:31`, `Halftime`, `Scheduled`) in 12pt at the top-left. Away team first (broadcast convention). Winner's abbreviation is white, loser's is muted; live games score in green.

Tap a card → game detail view: header `AWAY @ HOME` + status (`Final`, `Q2 05:31`), the score big, a box-score table (`Q1 Q2 Q3 Q4 [OT] T` with full team names), the game date + tip-off, and while a game runs the timeouts left. Needs no extra API call — the games list already carries the period scores, and `GET /games/{id}` returns the same object. `◀` returns to the board.

Memory budget: LVGL widgets live in LVGL's own pool (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`), measured on device — ~37% used after all screens exist, ~57% with a full page of score cards (each card ≈ 1.3KB). Keep an eye on it when adding cards/logos (`Rendered N card(s): lv pool …` in the serial log). A page of cards (~15KB) and the detail view (~12KB) do not sit side by side: tapping a card drops the page (measured 54% with the detail view open), the feed skips re-rendering into a hidden screen, the page is rebuilt from the cache when the board comes back, and the detail view frees itself on the way back — so the board still sits at 57% long after the first visit (keeping it alive cost 14 points). Fetched games sit in static arrays outside that pool (~13KB for the 4-page cache plus one API page). The worker also logs `cycle start, free heap: N` each refresh and reboots below 25 KB, because ESP-IDF's HTTPS stack keeps a couple of hundred bytes per connection.

API budget: the free balldontlie tier allows about **5 requests per minute**, so `nba_api` paces itself (13 s between calls, the clock-sync probe counts too) and failures back off 15 s → 5 min instead of retrying in a loop. Fetches ask for whole date *chunks* whose width adapts to how busy the slate is (1 day when a chunk costs two requests, up to 7 days when it stays thin), which keeps a page step at roughly one HTTPS call in either season. Paging inside the 4 cached pages costs nothing at all.

## Project Structure

```
cyd_basketball_scores/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── app_main.c          # Entry point, display init, LVGL port
│   ├── splash_screen.c/.h  # Splash with basketball + text
│   ├── cards_screen.c/.h   # NBA scores card list (renders the game boxes)
│   ├── game_detail_screen.c/.h # tap a card: box score by period + team names
│   ├── nba_api.c/.h        # BallDontLie NBA API client (games by date range)
│   ├── nba_service.c/.h    # Score feed worker: fetch one day page + push to cards screen
│   ├── time_sync.c/.h      # System clock sync from HTTPS Date header
│   ├── basketball_img.c/.h # LVGL image descriptor (64x64)
│   └── ...more screens
tools/
├── basketball.png          # Source image
├── convert_image.sh        # LVGL image converter script
├── sim/                    # Host LVGL preview harness (renders screens to PNG)
└── ui_tweaks.py            # Brightness/contrast tweak of the source image
.gitignore
TODO.md
README.md
```

## Building

### Prerequisites

- ESP-IDF (tested with v5.x / v6.x)
- LVGL managed component (resolved automatically)

### Quick Start

```bash
cd cyd_basketball_scores
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Image Conversion

If you want to replace the basketball logo:

```bash
# Replace tools/basketball.png with your image, then:
cd tools
./convert_image.sh
```

This uses the official [lv-img-conv](https://pypi.org/project/lv-img-conv/) Python package to generate LVGL-compatible C arrays.

### UI Preview (no hardware needed)

`tools/sim/` compiles the real screen sources against host LVGL and renders a
320x240 PNG, so layout changes can be checked without flashing:

```bash
tools/sim/run.sh          # -> /tmp/opencode/cards_screen.png
```

Mock game records (final / live / scheduled / postponed) live in
`tools/sim/main.c`. To preview another screen, add its `.c` to the target list
in `tools/sim/CMakeLists.txt` and stub whatever ESP-IDF symbols it needs (see
`tools/sim/shim/` and `tools/sim/stubs.c`).

## Current State

- **Boot flow**: On first boot (no NVS calibration) → calibration screen. After calibration → splash (3s) → cards
- **Touch**: XPT2046 calibrated, LVGL indev working, linear coordinate mapping
- **Calibration**: 4-corner crosshair calibration with long-press undo, quadrant validation, debounce
- **NVS**: Calibration data + saved flag persisted across reboots, validated on load (range 0-4095)
- **Settings**: Brightness slider (saves to NVS), Wi-Fi button, Touch Calibration button
- **Wi-Fi**: Scan, connect, saved networks (NVS), boot auto-connect, status badge — all working
- **NBA API**: [BallDontLie](https://www.balldontlie.io/) client (`nba_api.c`) — HTTPS + cJSON, date-range game queries; clock synced from the HTTPS `Date` header (`time_sync.c`)
- **Score cards**: `nba_service.c` worker fetches games on first Wi-Fi connect and every 15 minutes (on page 1 that re-anchors on today, so starting games and new slates come in), remembering which `season_type` answered so a refresh usually costs one HTTPS call per date chunk (October needs `preseason`); the cards screen renders one box per game (`AWAY  102 - 98  HOME`, date top-right, game clock/status top-left) and has a refresh button in the header
- **Paging**: the strip under the header (`<  Oct 14 - Oct 16  page 3  >`) steps through whole pages of games (8 each). balldontlie pages by *game id*, and ids do not track dates, so older pages are reached by moving the date window backwards (one day-chunk at a time, following `next_cursor` until the chunk is consumed), not by the cursor. Four pages stay in RAM: paging inside them is instant, stepping past the older edge fetches another chunk, and walking back off the newest edge re-anchors on today's games
- **Game detail**: tapping a card opens `game_detail_screen.c` — box score by period (OT column only when the game went to OT), full team names, game date + tip-off (labelled UTC, since `date` is the US game date and `datetime` is UTC), and timeouts left while a game is live. Its widgets are built on the first tap, and the card page is dropped while it is open (see Memory budget)
- **UI preview**: `tools/sim/` renders screens on the host (`tools/sim/run.sh`) — see "UI Preview" above
- **NBA API key**: set `NBA_API_KEY` in `main/defaults_override.h` (see `defaults_override.h.example`)

## Development Context (for AI agents)

### Stack
- **LVGL 9.5.0** (managed component at `cyd_basketball_scores/managed_components/lvgl__lvgl`)
- **ESP-IDF 6.0.1** (ESP32, not ESP32-S3)
- **C** (not C++) — no lambdas, use static C function callbacks

### Screen Navigation Pattern
- Screens are created once in `app_main.c` as static globals (`splash_scr`, `cards_scr`, `settings_scr`, `wifi_scr`)
- `lv_scr_load(screen_pointer)` switches screens
- All nav button callbacks use `lv_event_get_user_data(e)` to get the target screen pointer
- Callback signature: `static void nav_to_cards(lv_event_t *e) { lv_scr_load((lv_obj_t *)lv_event_get_user_data(e)); }`
- Registration: `lv_obj_add_event_cb(btn, nav_to_cards, LV_EVENT_CLICKED, (void *)cards_scr);`

### Color Palette
- Background: `0x16213e`
- Header: `0x0f3460`
- Text: `0xffffff`
- Subtle text: `0x555555`

### Display & Touch
- ST7789 rotated: `swap_xy=true, mirror_x=true, mirror_y=false`
- Touch: XPT2046 driver has `swap_xy=false, mirror_x=false, mirror_y=false`; coordinate swap done manually in `touch_process_coordinates`
- Backlight: GPIO 21, LEDC PWM

### Available Symbols (baked into Montserrat 16)
- `LV_SYMBOL_SETTINGS` — gear icon (for settings button)
- `LV_SYMBOL_WIFI` — Wi-Fi icon
- `LV_SYMBOL_LEFT` — left arrow (back button)
- `LV_SYMBOL_CLOSE` — close icon

### Current Screen Architecture
```
calibration (first boot only) → splash (auto, 3s) → cards
                                                                    ├── [refresh button] → refetch scores (nba_service)
                                                                    ├── [tap a card] → game detail (box score)
                                                                    │                    └── [back] → cards (page re-rendered from cache)
                                                                    └── [gear button] → settings
                                                                                         ├── [back] → cards
                                                                                         ├── [wifi btn] → wifi
                                                                                         │                    ├── [back] → settings
                                                                                         │                    └── [switch] → placeholder
                                                                                         └── [calibration btn] → calibration
                                                                                                                  ├── [back] → settings
                                                                                                                  └── [done & save] → splash → cards
```

### Calibration Screen Tutorial

Access via: **Cards screen → [gear button] → Settings → Touch Calibration**

**How it works:**

1. **Tap the crosshairs** in order (1 → 2 → 3 → 4) — top-left, bottom-left, bottom-right, top-right
2. Each tap records the raw XPT2046 ADC values and displays them in the center panel
3. **Long-press undo**: Hold any crosshair for ~1 second, then release. This undoes the last tap and returns you to the previous crosshair. The long-press is detected by LVGL's `LV_EVENT_LONG_PRESSED` — no timer needed.
4. **Quadrant checking**: Each tap must land in the expected quadrant (left/right for X, top/bottom for Y). If your tap is outside the expected quadrant, it's rejected with a warning logged to the serial console. This prevents misaligned calibration data.
5. **Debounce**: 500ms cooldown between taps. If you tap too quickly, the tap is ignored. This prevents accidental double-taps from corrupting the calibration.
6. When all 4 corners are collected, the "Done & Save" button appears. Tap it to save the calibration and return to the previous screen.

**Why quadrant checking?** The XPT2046 produces raw ADC values (0–4095). The midpoint is ~2047. Taps are validated against the expected quadrant before being recorded — this catches mis-taps (e.g., tapping the wrong corner or hitting the edge of the screen) before they corrupt the calibration matrix.

**Why debounce?** The XPT2046 can produce spurious readings when pressed too quickly. A 500ms cooldown ensures each tap is a deliberate, stable reading.

### Navigation Pattern
Each screen has a `*_screen_set_<target>_scr(lv_obj_t *scr)` setter used in `app_main.c`:
- `settings_screen_set_wifi_scr(wifi_scr)` — sets wifi target for settings screen's wifi button
- `settings_screen_set_cards_scr(cards_scr)` — sets cards target for settings screen's back button
- `settings_screen_set_calibration_scr(calibration_scr)` — sets calibration target for settings screen's calibration button
- `wifi_screen_set_settings_scr(settings_scr)` — sets settings target for wifi screen's back button
- `cards_screen_set_settings_scr(settings_scr)` — sets settings target for cards screen's gear button
- `game_detail_screen_set_back_scr(cards_scr)` — sets cards target for the detail screen's back button (the detail screen is created on the first card tap, so this is all the wiring it needs)
- `calibration_screen_set_done_cb(callback)` — sets callback for calibration "Done & Save" button
- `calibration_screen_set_back_scr(scr)` — sets back navigation target (settings screen)

### Known Issues
- `sdkconfig` may need `IDF_TARGET=esp32` (run `idf.py set-target esp32`)

### Lessons Learned
- **LVGL timer**: `lv_timer_create()` creates an infinite-repeat timer by default (`repeat_cnt=0`). Use `lv_timer_set_repeat_count(timer, 1)` for one-shot timers. The `lv_timer_t` struct is opaque — don't access members directly.
- **NVS validation**: Always validate calibration data ranges (0-4095 for XPT2046) before trusting the `cal_saved` flag. Corrupt NVS data can cause inverted touch mapping, making buttons auto-fire on wrong screens.
- **Flash and NVS**: Using `esptool merge-bin` may not erase the NVS partition. If calibration data is corrupt, run `idf.py erase-flash` before flashing, or rely on the built-in validation that resets corrupt data to defaults.
