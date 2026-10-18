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
- The app partition has ~170 KB free (17% after the NCAA logo trim). Any new baked data (art, larger fonts) must be measured against `check_sizes.py` first. Escape hatch: `tools/make_team_logos.py --all-conferences` toggle, or move rarely-needed logos to runtime fetch

### 1. Settings persistence bugs (user-reported) — DONE
- [x] Brightness: NVS round-trip worked all along, but the settings slider opened hardcoded at 128 → looked like the value was lost (boot log proves: `Brightness: 255` loaded). Slider now seeds from NVS, and NVS gets ONE write on release instead of one per drag pixel
- [x] Calibration: the saved blob was loaded at boot but **never applied to the touch handler** — reboots ran on 0..4095 defaults until recalibration. `touch_integration_apply_calibration()` now runs at boot when `calibration_saved`; `cal.swap_xy` (uninitialized stack slop in the blob — read back as 24) is set explicitly to the fixed orientation
- Sim gains a `settings` scenario (settings_screen + tiny esp_lcd_touch/driver-gpio shims): the knob renders at the saved 230/255, proving the seed

### 2. NCAA scores "don't work" (user-reported) — DONE
- Root cause was seasonal, not a parser bug: October = zero college events on ESPN (verified from the PC: Nov 1 = 2 games, Nov 3 = 29, Nov 10 = 69). The board dead-ended on "No data" and `<` stepped one empty day per 4 s request
- Now: when a college newest-edge fill comes back empty, the worker probes forward (jump by a week up to +28, then walk back inside the bracket) for the nearest upcoming slate and anchors the pager there — scheduled games (ESPN tip text) show instead of a dead board. The found day is remembered (`s_slate_day`) so the ~5 probe requests happen once per off-season day, not per cycle
- Found and fixed a real latent bug along the way: `page_slice()` refuses a **short page 0** unless `s_older_exhausted` is set, so ANY slate smaller than `SVC_PAGE_SIZE` parked the cache with games in it while the board showed "No games". The fill walk now counts empty chunks (same SVC_EMPTY_CHUNKS policy as page-stepping) and marks the older edge exhausted; a probed slate renders and greys out `<`
- Also: college chunk width is now clamped to 1 day inside `svc_fill_recent` (the static init is 2; the old clamp lived only in `set_league`, so a boot straight into a college league paid 2 ESPN requests per chunk)
- Device-verified with `-DCYD_TEST_LEAGUE=2`: anchored `day +15`, rendered the Nov 1-2 slate (2 cards), steady cost 3 requests/cycle; NBA production path unchanged (8 cards, 71% pool)

### 3. Cross-reboot score cache in NVS (user idea) — DONE
- One page (8 games, ~2.6 KB blob) per balldontlie league saved under `games_nba` / `games_wnba` — ESPN has no quota so college is not persisted; 2 blobs keep the 24 KB NVS partition comfortable. Generic `nvs_settings_save_blob`/`load_blob` helpers, strict size match
- `magic + sizeof(nba_game_t) + saved_at + league + count + games[]` header: struct drift or garbage self-rejects on load (also checked: magic, league tag, count bounds, epoch plausibility)
- Boot (and league-tab switches): the worker loads the snapshot before its first cycle — the board renders real scores in ~8 s even offline, `<` greyed until the walk re-earns it. A boot **inside** the refresh interval skips the first fetch outright (log `score cache is N s old - skipping this fetch`) — but never a manual refresh-button press
- Saved after each successful cycle only when the newest page actually changed (memcmp against the in-RAM mirror) — a finished slate costs zero flash writes
- Device-verified: NBA + WNBA save→load→skip round-trips; 8 cards from NVS with zero balldontlie calls
- Along the way, a latent boot race went deterministic with this build: `lv_theme_default_init`/`lvgl_port_add_touch` ran **unlocked** while the port task rendered since `lcd_init` — the binary layout shift turned it into a permanent boot hang in `lv_inv_area` (WDT barking, no screen). Both now take `lvgl_port_lock` (4 resets clean after the fix)

### 4. Score card polish
- API request budget: the free tier allows ~5 requests/minute. Handled today by `nba_api` pacing (13 s between calls) and the adaptive chunk width (college: ESPN fixed 1-day chunks, 4 s pacing), so a page step past the cache is about one HTTPS call and a refresh inside it is free. If paging still feels sluggish, raise `SVC_PAGE_SIZE` (more games per page) or `SVC_CHUNK_DAYS_MAX` (wider window in quiet stretches) rather than stepping more often — but note 8 cards is the LVGL pool ceiling with the league tabs

### 5. Touch rotation tuning
- XPT2046 driver flags (`swap_xy`, `mirror_x`, `mirror_y`) may need per-device tuning
- Current: driver flags all false, rotation handled in `touch_process_coordinates`

(End of file - total 55 lines)
