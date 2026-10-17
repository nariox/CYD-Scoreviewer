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
- [x] Full team names along the bottom of each card (12pt, away left / home right, `…` when longer than half a card). Cards grew 44px → 56px, so ~2.5 are visible; page of 8 now costs 65% of the LVGL pool
- [x] Team logos on the cards and in the detail view's score line: 30 teams baked into flash by `tools/make_team_logos.py` as ~22px RGB565A8 bitmaps (`main/team_logos.c`, generated), looked up by `team_id`; ~44KB flash, ~600B LVGL pool per card, zero HTTP. Source: cdn.nba.com SVGs (ESPN PNG fallback); `tools/logos/contact_sheet.png` verifies the id mapping by eye
- [x] WNBA logos baked too (13 current franchises, ESPN `wnba/500/<slug>.png`, 18 KB) behind `wnba_team_logo(id)` — balldontlie ids are per-league namespaces, so each league gets its own accessor. No caller yet, so `--gc-sections` keeps the flashed binary at its current size until a WNBA feed links the table

## In Progress (Blocking)

*(None — touch integration is complete and wired)*

## Next

### 0. Multi-league scores (WNBA / NCAAB / NCAAW) — feasibility measured Oct 2026
- **WNBA scores: nearly free.** balldontlie serves `/wnba/v1/games` with the same key and a near-identical game object (probed: 200 OK, fields match `nba_game_t`); logos are already baked. A feed is mostly a base-path parameter + a league tag on the game (to pick the right logo accessor)
- **NCAAB/NCAAW scores: balldontlie has them, but Games is paywalled.** `GET /ncaab/v1/games` and `/ncaaw/v1/games` exist on the same base URL and key — but the college plans meter access per sport: the Free tier covers conferences/teams/standings only (probed: `ncaab/v1/teams` → 200, 1364 teams; `ncaab|ncaaw /v1/games` → 401). Games need ALL-STAR at $9.99/mo **per sport** (NCAAB and NCAAW are separate subscriptions; tiers don't cross sports). The free fallback stays ESPN's undocumented scoreboard API (`site.api.espn.com/apis/site/v2/sports/basketball/{mens,women}-college-basketball/scoreboard`, no key, JSON) — different shape, would need its own parser. WNBA remains the only college-adjacent league whose *games* answer to the existing free key
- **College logos: the storage cliff — and the sharing you'd hope for is already counted.** ESPN serves ONE logo set for D1 men's and women's (`a.espncdn.com/i/teamlogos/ncaa/500/<espn_team_id>.png`; both league endpoints return the same 362 school ids), so the table costs one bitmap per school for both leagues together. Measured footprint at the card size (22px box, RGB565A8): avg 1.42 KB, median 1.45, max 1.76 → **~502 KB total for NCAAB+NCAAW**, not per league. Free app-partition space today is ~650 KB, so both college leagues together *fit* but would eat 77% of the headroom (leaving ~130 KB for all future growth). Recommended when/if a college feed ships: bake it then (`--league ncaa` block in the generator), or start with a ranked subset (Top 25 ≈ 36 KB), or fetch the ~16 visible games' logos at runtime with a small RAM LRU (~25 KB RAM, needs API-budget thought). Keying note: balldontlie college team ids are yet another per-league namespace (Duke = 4, and `/teams` spans all divisions — 1364 rows, D1 is ~362 of them); join to the ESPN logo set by the `college` field, and let non-D1 ids fall through to the no-logo text fallback

### 1. Score card polish
- API request budget: the free tier allows ~5 requests/minute. Handled today by `nba_api` pacing (13 s between calls) and the adaptive chunk width, so a page step past the cache is about one HTTPS call and a refresh inside it is free. If paging still feels sluggish, raise `SVC_PAGE_SIZE` (more games per page) or `SVC_CHUNK_DAYS_MAX` (wider window in quiet stretches) rather than stepping more often

### 2. NVS brightness persistence
- Brightness slider already saves to NVS on change — verify it loads on boot

### 3. Touch rotation tuning
- XPT2046 driver flags (`swap_xy`, `mirror_x`, `mirror_y`) may need per-device tuning
- Current: driver flags all false, rotation handled in `touch_process_coordinates`

(End of file - total 55 lines)
