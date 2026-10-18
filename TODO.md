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
- [x] Multi-league board: header tabs `NBA | MBB | WBB` (lit = active feed; clickable labels + shared `lv_style_t` for pool), `league_t` on `nba_game_t`, `game_logo(league,id)` dispatch in `league_logos.c/h`, detail view per league (MBB halves `H1/H2/OT`, WBB quarters, timeouts NBA-only), `nba_service_set_league()` cache+pager reset done by the worker
- [x] College scores (MBB/WBB) without paying for a tier: ESPN site scoreboard API client `espn_api.c/h` — browser UA, 1 request/day, own 4 s pacing; constant-memory streaming JSON scanner (the body pads each event to ~20 KB, cJSON would OOM), keeps each day's 24 newest games; ESPN `dates=YYYYMMDD` day semantics match `time_sync_game_day_epoch()`. **Caveat:** only the network-covered D1 slate (~10-19/day, 60+ in November), not all ~350 games
- [x] Parser regression harness: `tools/sim/run.sh` runs `espnparse` over captured scoreboard fixtures (`tools/sim/fixtures/espn_*.json` — final days both leagues + a 69-event scheduled day), asserting halves-vs-quarters, OT linescores, the scheduled 0-0 guard, the newest-24 cap; caught 3 device-visible bugs pre-flash
- [x] All 362 D1 school logos baked (ESPN `ncaa/500/<id>.png`, shared MBB+WBB set, ~507 KB; total logo bitmaps ~569 KB) — device-verified on real Jan 24 games via `-DCYD_TEST_LEAGUE`/`-DCYD_TEST_DAY_SHIFT` hooks; binary ~1.98MB, app partition ~4% free
- [x] WNBA scores tab: header is `NBA | WNBA | MBB | WBB` (labels at `UI_TAB_W` 33 so four fit), balldontlie `/wnba/v1/games` on the same free key via `nba_api_url_nba()/url_wnba()` + a `parse_game` payload branch (`date`=UTC datetime, top-level `away/home_score`, `status_state`, no period stats → detail hides the table); season_type fallback gated to NBA. Device-verified on live playoff games (`-DCYD_TEST_LEAGUE=1`)
- [x] NCAA logo trim to power-5 + group-of-5 core (182 schools, membership from one ESPN standings call cached as `ncaa_conferences.json`; `--all-conferences` opt-out): bitmaps 569→315 KB, binary ~1.74MB, app partition **4%→17% free**; unlisted schools draw a runtime placeholder ring (device-verified Jan 24 slate via `-DCYD_TEST_LEAGUE=2 -DCYD_TEST_DAY_SHIFT=-266`)
- [x] Shorter left-aligned cards (56→46 px, ~3.3 visible): main row top-aligned at the date's y, centre text capped at 100 px (16pt + `…` past 12 chars), live clock moved to the bottom band between the names (floating children align to the *padding* box — use TOP anchors + `UI_CARD_NAME_ROW_Y`); empty state adds a "Check Wi-Fi, or try another tab" hint

## In Progress (Blocking)

*(None — touch integration is complete and wired)*

## Next

### 0. Flash headroom — now the binding constraint
- The app partition has ~88 KB free (~4%). Any new baked data (art, larger fonts, a WNBA-score feed — WNBA logos would add nothing, they're already in) must be measured against `check_sizes.py` first. Escape hatches, in order: trim the NCAA logo set to a ranked Top-N subset (regenerates in one command, buys ~400 KB), or move rarely-needed logos to runtime fetch with a RAM LRU

### 1. Score card polish
- API request budget: the free tier allows ~5 requests/minute. Handled today by `nba_api` pacing (13 s between calls) and the adaptive chunk width (college: ESPN fixed 1-day chunks, 4 s pacing), so a page step past the cache is about one HTTPS call and a refresh inside it is free. If paging still feels sluggish, raise `SVC_PAGE_SIZE` (more games per page) or `SVC_CHUNK_DAYS_MAX` (wider window in quiet stretches) rather than stepping more often — but note 8 cards is the LVGL pool ceiling with the league tabs

### 2. NVS brightness persistence
- Brightness slider already saves to NVS on change — verify it loads on boot

### 3. Touch rotation tuning
- XPT2046 driver flags (`swap_xy`, `mirror_x`, `mirror_y`) may need per-device tuning
- Current: driver flags all false, rotation handled in `touch_process_coordinates`

(End of file - total 55 lines)
