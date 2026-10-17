# TODO

## Done

- [x] ESP-IDF project scaffold (CMake, sdkconfig, deps)
- [x] LVGL 9 integration with ST7789 SPI display on CYD2USB
- [x] Splash screen: basketball icon + "Sami's basketball" / "score viewer"
- [x] Cards screen: "NBA SCORES" header + empty scroll container
- [x] 3-second splash then transition to cards screen (one-shot LVGL timer)
- [x] Pinout for CYD2USB (HSPI: MOSI=13, MISO=12, SCLK=14, CS=15, DC=2, RST=4, BL=21)
- [x] Clean build artifacts and .gitignore
- [x] tools/ directory with source PNG and conversion script
- [x] Settings screen with static C callbacks (no lambdas)
- [x] Wifi screen with back button, Wi-Fi switch, network list, IP config
- [x] Cards screen with gear/settings button in header
- [x] LEDC backlight PWM init on GPIO 21
- [x] BOYA flash chip, 4MB flash, single app partition config
- [x] Touch poll task (50ms) + LVGL indev registration
- [x] Touch test screen (9 buttons + live coords label)
- [x] Calibration screen (4 corners, raw ADC display, long-press undo, quadrant validation)
- [x] Calibration button in settings screen
- [x] Touch integration rewrite: LVGL indev callback, XPT2046 mapping, no redundant poll task
- [x] Touch integration wired into app_main.c (splash → cards, settings → calibration)
- [x] Calibration screen wired into boot flow (NVS check for calibration data)
- [x] NVS calibration persistence (save/load calibration data + saved flag)
- [x] NVS calibration validation (range check 0-4095, x_max>x_min, y_max>y_min)
- [x] Calibration back navigation (calibration_screen_set_back_scr to return to settings)
- [x] Calibration state reset when accessed from settings (calibration_screen_reset)
- [x] Splash timer fixed to one-shot (lv_timer_set_repeat_count = 1, not infinite)
- [x] Wi-Fi backend: esp_wifi state machine (scan/connect/retry timer), scan on worker task, credentials saved to NVS
- [x] Wi-Fi screen live flow: scan networks → SSID/password entry with keyboard → connect; saved networks list with delete
- [x] Boot auto-connect to last saved network; splash holds until connected (or falls back after 5s with retry timer)
- [x] Wi-Fi status badge on cards header (green/amber/red by connection state)
- [x] Thread-safe Wi-Fi/UI integration: lvgl_port_lock around every cross-task LVGL call
- [x] NBA API client (BallDontLie v1): `nba_api.c/h` — HTTPS via esp_http_client + esp_crt_bundle (cross-signed verify on), cJSON via espressif/cjson managed component, date-range fetch + preseason fallback, ~15-game memory budget
- [x] Clock sync for API date windows: `time_sync.c/h` — parses HTTP `Date` header from the API host (no NTP), sets system time, formats "YYYY-MM-DD" windows
- [x] Console sample: on first Wi-Fi connect, one-shot `nba_demo_task` prints last-7-days games (preseason fallback) to serial
- [x] Score cards on the cards screen: one box per game — `AWV  score - score  HOM` (winner white, loser muted, live score green), date in 12pt at the card's top-right; scheduled games show their time, live games the game clock
- [x] `nba_service.c/h`: resident score-feed worker (FreeRTOS task) — syncs clock, fetches games, remembers the `season_type` that answered, pushes cards under the LVGL port lock, auto-refreshes every 15 min, manual refresh via the header button (binary semaphore), reboots below 25 KB free heap
- [x] Paging: `<` / `>` strip under the header (`Oct 14 - Oct 16   page 3`), a page = a fixed card count (`CYD_NBA_PAGE_SIZE`, 8) so a busy day spans pages and a quiet stretch spans days; 4 pages cached, older pages fetched a 3-day chunk at a time, `next_cursor` followed inside a chunk, `time_sync_game_day_epoch()` anchors day 0 to the US game date (clock − 12 h)
- [x] Offline UI preview harness: `tools/sim/` builds the real `cards_screen.c` against host LVGL and dumps a 320x240 PNG (mock games for final / live / scheduled / postponed states)
- [x] Tap a card → game detail view (`game_detail_screen.c/h`): box score by period (OT column only when used), full team names, game date + tip-off, timeouts left while a game runs. Built from the cached list response — `GET /games/{id}` adds nothing, so a tap costs no HTTP request

## In Progress (Blocking)

*(None — touch integration is complete and wired)*

## Next

### 1. Score card polish
- API request budget: the free tier allows ~5 requests/minute. Handled today by `nba_api` pacing (13 s between calls) and the adaptive chunk width, so a page step past the cache is about one HTTPS call and a refresh inside it is free. If paging still feels sluggish, raise `SVC_PAGE_SIZE` (more games per page) or `SVC_CHUNK_DAYS_MAX` (wider window in quiet stretches) rather than stepping more often

### 1. Team logos on the cards (decision needed: logos vs full names)
Feasible, but only as **build-time bitmaps**:
- balldontlie has no logo URLs, but the NBA CDN does: `https://cdn.nba.com/logos/nba/{1610612736 + team_id}/primary/L/logo.svg` — the API's team `id` maps 1:1 (1 = ATL = 1610612737) and the SVGs are tiny (1.2–1.9 KB). `nba_game_t` now carries `home_id`/`visitor_id`, so the lookup is ready.
- On-device SVG is out: `CONFIG_LV_USE_THORVG` is off and rasterizing vector paths on an ESP32 with a 64 KB LVGL pool is not sane. Runtime PNG would need a decoder component *and* 30 fetches per cold start against the API budget.
- So: convert 30 logos to bitmaps on the PC (the `tools/convert_image.sh` pattern) and keep them as const arrays in flash. RGB565A8 is ~3 bytes/px: 24px ≈ 1.7 KB each (**~52 KB** for 30 teams), 32px ≈ 3 KB (**~90 KB**) — out of ~700 KB still free in the app partition. The pixels stay in flash (const arrays); an `lv_image` widget costs only ~400 B of LVGL pool per card. Indexed colour (I4) would halve the flash but loses per-pixel alpha, which is what makes the logo edges look clean on the dark background.
- Layout: a 24px logo fits inline on the card's centre row and costs **no extra card height**. Full team names instead would need a third text band → cards grow from 44px to ~58px, i.e. ~2.5 cards visible instead of ~3. (The detail view already shows the full names.)
- `ESPN` PNG endpoint (`a.espncdn.com/combiner/i?img=/i/teamlogos/nba/500/atl.png`) is a working PNG source on the PC if the CDN SVGs convert badly.

### 2. NVS brightness persistence
- Brightness slider already saves to NVS on change — verify it loads on boot

### 3. Touch rotation tuning
- XPT2046 driver flags (`swap_xy`, `mirror_x`, `mirror_y`) may need per-device tuning
- Current: driver flags all false, rotation handled in `touch_process_coordinates`

(End of file - total 55 lines)
