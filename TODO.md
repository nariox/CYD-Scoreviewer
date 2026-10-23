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
- [x] Settings persistence fixed (user report "touch and brightness don't save"): calibration blob now actually **applied to the touch handler at boot** (was loaded and dropped — reboots ran on 0..4095 defaults); `cal.swap_xy` pinned to the fixed orientation (was uninitialized blob slop); brightness slider seeded from NVS (was hardcoded 128) and NVS written once on release, not per drag pixel. Sim gained a `settings` scenario; device-verified
- [x] College off-season no longer a dead board (user report "NCAA doesn't work at all" — the season just hadn't started): when a college newest-edge fill comes back empty the worker probes forward (week-jump to +28 d + bracket walk-back, ~5 requests, remembered in `s_slate_day`) and anchors the pager on the nearest upcoming slate — scheduled games with tip text. Along the way: short-page-0 fix (a slate smaller than `SVC_PAGE_SIZE` used to park the cache while the board said "No games"; the fill walk now tallies empty chunks and marks the older edge exhausted) and a college chunk-width clamp at the use site. Device-verified (`day +15`, Nov 1-2 slate)
- [x] Cross-reboot score cache in NVS (user idea): one page (8 games, ~2.6 KB) per balldontlie league (`games_nba`/`games_wnba`; college not persisted — ESPN has no quota), saved only when the newest page changed, loaded at worker start / league switch → boots render in ~8 s with **zero API calls**, and a boot inside the refresh interval skips the first fetch. Generic `nvs_settings_save_blob`/`load_blob`; blob header carries `sizeof(nba_game_t)` so struct drift self-rejects
- [x] Boot hang fixed (surfaced by the cache build): `lv_theme_default_init`/`lvgl_port_add_touch` ran unlocked while the port task rendered since `lcd_init` — a latent race the binary-layout shift made deterministic (WDT barking, blank screen, parked in `lv_inv_area`); both take `lvgl_port_lock(0)` now
- [x] Splash boot-stage line (user request): the splash now says what is happening — "Connecting to Wi-Fi..." → "Wi-Fi up - loading scores..." / "Wi-Fi failed - opening setup..." from the Wi-Fi callback, and "Showing stored scores..." / "Fetching first scores..." from the feed worker (guarded by `splash_screen_is_current()`, port-lock held). Sim gained a `splash` scenario
- [x] MBB/WBB tabs renamed to `NCAA`/`NCAAW` (user request — customary names): labels measured per tab (`lv_text_get_size` + `UI_TAB_PADX`; uniform "NCAAW"-sized boxes pushed the gear off-screen). `SIM_HEADER_DUMP` sim print verifies the header fit
- [x] College tabs pre-warm at boot (user request "takes a while to load"): after the first successful cycle the worker walks the college leagues it is NOT showing (ESPN-only — no quota) into RAM slots (`s_prewarm`, deliberately not NVS — a 3rd/4th 2.6 KB blob starves the 24 KB partition's GC), `s_slate_day` became per-league `s_slate_day[]` so the probed day survives for the tab's refill, and `svc_cache_load` now ends in an early `svc_render` — the tab switch paints in ~110 ms (device-measured) instead of a 20-40 s hunt. `-DCYD_NBA_AUTOTAB=1` soak hook hops NCAA↔NBA to prove it without a finger. (The "only future games" part is the off-season, not a bug: November 1 is the first slate.)
- [x] **League-switch race fixed** (user report "filters show games from other leagues"): `nba_service_set_league()` used to wipe `s_league`/cache straight from the LVGL tab callback while the worker was mid-fetch — the in-flight cycle (now 40-90 s with pre-warm) kept merging the OLD league's games into the freshly-wiped cache and rendered them under the new tab. Feed state is now worker-owned: a tap only queues `s_pending_league`, the worker applies it at a cycle boundary, every fetch/hunt loop bails at its next request boundary, and a switch-pending cycle never renders or persists. Also: the NVS blob load now rejects games whose `league` mismatches the feed (self-heals snapshots the buggy build wrote), backoff sleep is interruptible by a tap/refresh, and the tab highlight follows the tapped label (not the lagging service state). `-DCYD_NBA_AUTOTAB=2` race soak hook flips the feed from a timer mid-first-cycle. Device-verified
- [x] Preseason college coverage confirmed against ESPN (user question "MSU has 3 preseason games — does ESPN not cover these?"): the scoreboard **does** list exhibitions — Nov 2 has 181 events including Quinnipiac @ MSU, MSU @ Duke is on Nov 10 — it simply has *zero game days* before Nov 1, which is why the off-season hunt parks there. Nothing to fix; MSU's games land on the NCAA tab on their days
- [x] Future-dated cache garbage dropped (found verifying the switch fix): the dev board's NVS snapshot held 4 real NBA games dated Nov 1 (saved by a `CYD_TEST_DAY_SHIFT` soak build) and re-anchoring walks only backwards, so they sat on the newest edge permanently — `svc_fill_recent` now drops games beyond `max(today, slate) + 1 day` and the next persist rewrites a clean snapshot

- [x] Theme/button pass (user's "eventually" list, 2026-10): lit league tab + busy comet now wear a per-league accent (NBA crimson / WNBA orange / NCAAB cyan / NCAAW magenta — covers the "NCAA magenta/cyan or NBA red/blue" ask by making the accent follow the tab); pager arrows became visible 36x24 panels (header-coloured, 1 px `UI_COL_BORDER`) instead of bg-on-bg, header icons ghost-bordered; header trimmed 50→44 px and card gap 8→12 px + 4 px top offset (cards breathe, a 3-card page is 162 px inside 170 — still no scroll); secondary text lifted off 0x555555. Sim PNGs eyeballed (accents pixel-verified per tab), device render at the same 75% pool
- [x] College NVS snapshots (user: "even 3 games would be sufficient"): `games_ncaab`/`games_ncaaw` persist one page (~1.1 KB/league, knob `CYD_NBA_PERSIST_GAMES_COLLEGE`) while the pro boards keep the 12-game box; blob entries are now variable length (header + count games, loader validates the stored size against count — old full-size pro blobs load unchanged); device-verified round-trip (AUTOTAB persist, then a `CYD_TEST_LEAGUE=2` cold-box boot painted the stored Nov 1 slate from flash at ~1.5 s). Device queue item "WNBA 12-game blob round-trip" stays open (the WNBA blob re-saved at 12 games across these soaks but was never reboot-loaded since)
- [x] Page-5 skip fixed (user report "page 5 jumps to Oct 20th, skipping Oct 22nd"): the backwards day-walk resumed from `s_lo_day` — the oldest day ever fetched — but a page-1 re-anchor evicts already-fetched days out of the 12-game sliding window and the min-ratcheted cursor stayed parked where yesterday's idle prefetch left it, so `<` stepped past the gap and hid a whole day for the session. The walk now resumes below the **oldest game in the window**, refetches that day once when the re-anchor chopped part of it off (dupes merge; once-only so a day bigger than the window can't bounce), and skips re-probing a day already proven empty (single-empty-day gaps stay crossable). Device build in place; confirm: page 5 lands on Oct 22

## In Progress (Blocking)

*(None — touch integration is complete and wired)*

## Next

### 0. Flash headroom — the binding constraint for anything baked
- The app partition has ~170 KB free (17%). Any new baked data (art, larger fonts, more logos) must be measured against `check_sizes.py` output first. Escape hatch: `tools/make_team_logos.py --all-conferences` toggle, or move rarely-needed logos to runtime fetch

### 1. Touch driver tuning — DONE (2026-10)
- **Rotation flags stay all false** (`swap_xy/mirror_x/mirror_y`). `esp_lcd_touch` applies them *after* the `process_coordinates` callback, so a driver-side swap/mirror would flip already-calibrated screen coords. Orientation is absorbed by the calibration data (4-point raw min/max map + the calibration's own `swap_xy` flag) — a different panel needs recalibration, not a flag change
- **Enabled `CONFIG_XPT2046_INTERRUPT_MODE=y`** (sdkconfig.defaults): every read is gated on the PENIRQ pin (GPIO36, active low — wired on the CYD and already passed as `int_gpio_num`); idle polls do no SPI, and ghost readings need physical pen contact to reach the Z check at all
- Knobs if the feel ever sours: `CONFIG_XPT2046_Z_THRESHOLD` (default 400) up = rejects lighter presses; `CONFIG_XPT2046_ENABLE_LOCKING` stays off (driver's own warning)
- Pending on the next screen test: taps responsive, no ghost marks with interrupt mode on

### 2. Tuning knobs already in place (no action pending)
- API budget: ESPN paces itself at 4 s between requests, one keep-alive handshake per burst, 4-page RAM cache + NVS snapshot + boot pre-warm mean a refresh is usually a handful of polite day-requests per 15 min; if paging ever feels sluggish raise `SVC_PAGE_SIZE` (3 = exactly the 164 px band, a page never scrolls — more needs the LVGL pool back up) or `SVC_IDLE_PREFETCH_DAYS`
- Refresh cadence is already 15 min (longer than the 10 min floated during the old filter-bug report — that latency was the switch race, not refresh spam; the ESPN day-walk model never hammers the API anyway)

### 3. Compact game storage (user idea — parked, not needed yet)
- Store `(league, team ids, scores, date…)` instead of ~214 B of `nba_game_t` strings + a side table for teams outside the baked logo sets; would shrink the ~20 KB of statics ~6x and allow a much deeper cache
- Not done because the numbers do not demand it yet: 32-game cache + API page ≈ 20 KB of a ~70 KB heap, the pager only shows 8 cards, and the cross-league bug that motivated the idea was a serialisation race, not storage. Revisit if the cache ever needs to hold weeks of history or more leagues (the negative-id + linked-list encoding is the right shape then — plus names must survive for non-baked small-school preseason teams)

### 4. Port to LVGL 9.6 (user request, 2026-10)
- Raise the pin in `main/idf_component.yml` (`lvgl/lvgl ==9.5.0` → `^9.6`). The blocker: 9.6 flips deprecation warnings on our `CONFIG_LV_*` sdkconfig keys into `-Werror` failures — the real work is renaming/pruning those keys in `sdkconfig.defaults` and the sim's `tools/sim/lv_conf.h`
- Re-audit after the bump: the render paths we lean on (flex layout, RGB565A8 draw-buffer math for the baked logos, spinner, `lv_text_get_size` tab sizing) and re-run `tools/sim/run.sh`; the 48 K LVGL pool headroom was measured on 9.5, so re-check the `lv pool …%` lines too

(End of file - total 55 lines)
