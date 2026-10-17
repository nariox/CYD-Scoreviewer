# AGENTS.md — CYD Basketball Scores

ESP-IDF + LVGL 9 embedded C project for CYD2USB (ESP32, ST7789 320×240).

## Commands

Source ESP-IDF first: `source /opt/esp-idf/export.sh`

```bash
cd cyd_basketball_scores
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Clean build: `idf.py fullclean` (removes `build/`, `managed_components/`, `.cache/`).

Soak-test the score feed (short refresh interval, watch the heap/lv-pool logs):

```bash
idf.py -DCYD_NBA_REFRESH_S=45 build && idf.py -p /dev/ttyUSB0 flash
idf.py -DCYD_NBA_REFRESH_S= build            # back to the 15-minute default
```

Exercise the pager without touching the screen (the worker walks a few pages deeper each
cycle, then snaps back to the newest):

```bash
idf.py -DCYD_NBA_AUTOPAGE=1 -DCYD_NBA_REFRESH_S=45 build && idf.py -p /dev/ttyUSB0 flash
```

Exercise the card-tap → detail paths the same way (the worker opens the detail view
for the newest game once, so the pool handover and the re-render on the way back get
logged without a finger on the glass):

```bash
idf.py -DCYD_NBA_AUTODETAIL=1 -DCYD_NBA_REFRESH_S=45 build && idf.py -p /dev/ttyUSB0 flash
```

Page size (cards per page, default 8): `idf.py -DCYD_NBA_PAGE_SIZE=12 build`.
Keep it at 8: cards are ~2.6KB of LVGL pool each, and with the league-tab header
12 per page already sits at ~99% of the 64KB pool (sim-measured) — 12 is out of reach.

Exercise the college feeds without a finger on the tabs (start straight on MBB=2 /
WBB=3, and anchor the feed's "today" N days off the real one — college is in the
off-season in October, so shift onto a January slate to see real games/logos):

```bash
idf.py -DCYD_TEST_LEAGUE=2 -DCYD_TEST_DAY_SHIFT=-266 -DCYD_NBA_REFRESH_S=45 build && idf.py -p /dev/ttyUSB0 flash
idf.py -DCYD_TEST_LEAGUE= -DCYD_TEST_DAY_SHIFT= build   # back to production
```

NOTE: `idf.py` caches `-D` options in `build/CMakeCache.txt` — a plain
`idf.py build` after a test build **keeps flashing the hooks**. Clear them with
the empty `-DVAR=` form above.
NOTE: any `target_compile_definitions` value containing parentheses must be **quoted** in
`main/CMakeLists.txt` — CMake splits `(45*1000)` as list syntax and the build dies in the
shell with a stray `-D)`.

## Architecture

- **Entry point**: `main/app_main.c` — creates all screens, wires navigation, starts LCD + touch
- **Screens** (all created in `app_main.c` as static globals):
  - `splash_screen` — splash with spinner, 3s one-shot auto-transition
  - `cards_screen` — league tabs (`NBA | MBB | WBB`, lit tab = active feed; clickable labels sharing one static `lv_style_t` — lv_btns cost two widgets each) + date pager strip + scrollable card list (one day per page); tapping a card opens the game detail view
  - `game_detail_screen` — box score by period + team names + tip-off (**built on the first tap and freed again on the way back**, not at boot: the card page is dropped while it is open, see the pool note in README). Per league: NBA/WBB play quarters (`Q1..Q4`), MBB plays halves (`H1 H2`) — the OT column sums whatever period slots sit beyond the league's regulation count; timeouts row is NBA-only (ESPN doesn't carry them)
  - `settings_screen` — brightness slider (NVS-persisted), Wi-Fi/nav buttons, calibration button
  - `wifi_screen` — Wi-Fi config UI (scan/connect/save, wired to `wifi_backend`)
  - `touch_test_screen` — 9-button touch test (**not created at boot any more**: every screen costs LVGL pool; build it in only when debugging touch)
  - `calibration_screen` — 4-point crosshair touch calibration with long-press undo, wired into boot flow
- **NBA data**: `nba_api.c/h` — BallDontLie v1 client (`/nba/v1/games`, date-range query, `Authorization` header key via `defaults_override.h` → `NBA_API_KEY`), and the shared `nba_game_t` / `league_t` (NBA/WNBA/NCAAB/NCAAW) every screen and cache uses; `time_sync.c/h` — sets system clock from the HTTPS `Date` header (no SNTP), formats `YYYY-MM-DD` windows for the API
- **College data (MBB/WBB)**: `espn_api.c/h` — ESPN site scoreboard client (`site.api.espn.com/.../{mens,womens}-college-basketball/scoreboard?dates=YYYYMMDD`, no key, needs `User-Agent: Mozilla/5.0`). Serves only the ~10-19 network-covered D1 games/day (60+ in November) — not the full D1 slate. The body pads each event to ~20 KB (200-400 KB/day), so **no cJSON**: a constant-memory streaming scanner (`espn_parse_scoreboard`, brace-depth + container name/key tracking) assigns values only at the schema points the board renders, keeping each day's **24 newest** games (ESPN orders events oldest-tip-first — a plain cap keeps the wrong half of a busy day). One request per day, `ESPN_API_MIN_GAP_MS` (4 s) pacing. Host-tested against real fixtures: `tools/sim/run.sh` → `espnparse`
- **Logo dispatch**: `league_logos.c/h` — `game_logo(league, id)` picks the right table (`team_logo` 1..30 / `wnba_team_logo` 1..13 / `ncaa_team_logo` ESPN school ids, shared by MBB+WBB). Never pick by the raw id number: all three namespaces overlap
- **Team logos**: `team_logos.c/h` — **generated** (do not edit) const RGB565A8 bitmaps, ~22 px, for the 30 NBA + 13 current WNBA + 362 NCAA D1 schools (~569 KB of bitmaps total — the NCAA set alone is ~507 KB and **all of it is linked now**, keep an eye on partition headroom). balldontlie NBA and WNBA ids are separate overlapping namespaces, and ESPN school ids are a third, so there is one accessor per league: `team_logo(id)` (1..30) / `wnba_team_logo(id)` (1..13) / `ncaa_team_logo(id)` (ESPN school ids, one set shared by MBB+WBB) — pick by the feed the game came from, never by the number; screens call them via `game_logo(league, id)` in `league_logos.c/h`. Regenerate with `python3 tools/make_team_logos.py [--league nba|wnba|ncaa|all]` (NBA: cdn.nba.com SVGs, ESPN fallback; WNBA: ESPN `wnba/500/<slug>.png`; NCAA: ESPN `ncaa/500/<espn_id>.png` + the school list from ESPN's leagues endpoint; team lists from balldontlie `/teams` endpoints, WNBA roster baked as fallback since that endpoint 404s intermittently). Emits a labelled `tools/logos/contact_sheet.png` — eyeball it, the CDN ids are not derivable from balldontlie ids (and ESPN school ids are not guessable either: UK=96, MSU=127, NKU=94). Pixels live in flash; only the `lv_image` widget costs LVGL pool (~600 B/card). Generator symbol tables are keyed per league with prefixed names (the 362 NCAA abbreviations are not unique)
- **Score feed**: `nba_service.c/h` — resident FreeRTOS worker started on the first Wi-Fi connect (`app_main.c`); keeps a newest-first cache of 4 pages of games (`SVC_PAGE_SIZE`, 8 by default), fetches whole date **chunks** whose width adapts to how busy the slate is (`SVC_CHUNK_DAYS_*`, 1–7 days; college rides ESPN with a fixed 1-day chunk, 1 request per day) following `next_cursor` until the chunk is consumed (remembering which `season_type` answered), then calls `cards_screen_show_games()` / `cards_screen_show_message()` inside `lvgl_port_lock()`. `nba_service_set_league(lg)` (cards-screen tabs) resets cache+pager and sets `s_league_switched` — the **worker** shows "Loading..." (the tab callback runs with the port mutex held; it is not recursive). Refresh every `NBA_SERVICE_REFRESH_MS` (override: `idf.py -DCYD_NBA_REFRESH_S=45 build`): on page 1 it re-anchors on today (picks up starting games and new slates), deeper pages are refetched only by the header button or when they hold unfinished games. `nba_service_request_refresh()` and `nba_service_step_page(delta)` (pager arrows) both give the wake binary semaphore. `svc_render()` skips pushing a page into a **hidden** screen (`cards_screen_page_visible()`) — the detail view needs that LVGL pool — and `nba_service_rerender()` rebuilds the page from cache when the board comes back (no HTTP)
- **NVS**: `nvs_settings.c/h` — calibration data + saved flag persistence with validation (range 0-4095)
- **UI constants**: `ui_common.h` — shared dimensions (header heights, row widths, keyboard height…) and color palette macros for all screens; no magic numbers in screen files
- **Hardware**: `hardware.h` (all pin defines, LCD/touch/LEDC constants), `lcd.c/h` (SPI init + backlight PWM)
- **Touch**: `touch_integration.c` — XPT2046 on SPI3, linear mapping via `map()`, LVGL indev read callback

## Navigation Pattern

Every screen has a `*_screen_create(void)` factory. Navigation uses setter functions + static callbacks:

```c
// In app_main.c, after creating screens:
cards_screen_set_settings_scr(settings_scr);
settings_screen_set_wifi_scr(wifi_scr);
settings_screen_set_cards_scr(cards_scr);
wifi_screen_set_settings_scr(settings_scr);

// In a screen .c, static callback:
static void nav_to_settings(lv_event_t *e) {
    lv_obj_t *target = (lv_obj_t *)lv_event_get_user_data(e);
    lv_scr_load(target);
}
// OR using setter + NULL user_data (see cards_screen.c settings_btn_event_cb)
```

**Adding a new navigation link**: create setter in target screen header, call it from `app_main.c`, add static callback in source screen.

## Constraints

- **C only** — no lambdas, no C++. All callbacks are `static void fn(lv_event_t *e)`.
- **ESP32** (not ESP32-S3) — different SIMD optimizations, different GPIO.
- **Display rotation**: `swap_xy=true, mirror_x=true, mirror_y=false` (in `lcd.c` and `touch_integration.c`).
- **Touch rotation**: must match display — `swap_xy=true, mirror_x=false, mirror_y=false` (XPT2046 driver config in `touch_integration.c:78`).
- **Flash**: 4MB, BOYA chip, single app partition (`sdkconfig.defaults`). The binary is ~1.98MB / 2MB partition (**~4% free**) with all logo tables linked — flash is the scarce resource; measure `check_sizes.py` output before baking more art or tables.
- **Backlight**: LEDC PWM on GPIO 21, 5kHz. `lcd_brightness_set(duty)` sets LEDC duty directly.
- **LVGL fonts**: Montserrat 12, 16, 20, 48 enabled.
- **Perf build**: `sdkconfig.defaults` sets `CONFIG_COMPILER_OPTIMIZATION_PERF` (-O2) and 240MHz CPU. LCD SPI runs at 40MHz (`LCD_PIXEL_CLOCK_HZ` in `hardware.h`, already max reliable on this board's wiring). Do NOT enable `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` — LVGL 9.5's `LV_ATTRIBUTE_FAST_MEM` declarations collide and fail `-Werror`. `-O2` surfaces `-Wstringop-truncation`/`-Wformat-truncation` in esp_wifi ssid/password `strncpy` copies: use `strnlen` + `memcpy` into the zero-initialized `wifi_config_t` fields.
- **Color palette**: bg `0x16213e`, header `0x0f3460`, text `0xffffff`, subtle `0x555555`.

## Touch Integration (Complete)

Current state: touch hardware init works, LVGL indev registered, calibration screen wired into boot flow.
- Boot flow: NVS check → if no valid calibration, load calibration screen; if valid, splash → cards
- Calibration: 4-corner crosshair, quadrant validation, 500ms debounce, long-press undo
- Back navigation: calibration_screen_set_back_scr(settings_scr) for returning from settings
- Reset: calibration_screen_reset() called before loading calibration from settings

## Dependencies

Managed via `main/idf_component.yml`:
- `espressif/esp_lvgl_port: ^2.3.0`
- `espressif/esp_lcd_touch: ^1.2.1`
- `atanisoft/esp_lcd_touch_xpt2046: ^1.0.6`
- `espressif/cjson: ^1.7.19` (cJSON is NOT built into ESP-IDF ≥6.0)
- `lvgl/lvgl: ==9.5.0` — **pinned**; 9.6 turns deprecation warnings on our LVGL sdkconfig keys into `-Werror` failures

## Image Conversion

```bash
cd tools
./convert_image.sh
```

Requires `lv-img-conv` Python package. Outputs LVGL C array to `main/`.

## Team Logos

`main/team_logos.c` is generated — regenerate (never hand-edit) with:

```bash
python3 tools/make_team_logos.py
```

Needs `pillow` + `cairosvg`. It caches SVG sources + a labelled contact sheet
under `tools/logos/` (gitignored). Check the contact sheet before committing:
balldontlie `team_id` is not a linear offset into the NBA.com CDN id.

## UI Preview (host, no hardware)

```bash
tools/sim/run.sh    # -> /tmp/opencode/cards_screen.png (320x240)
```

Compiles the real screen `.c` files against host LVGL (`lv_conf.h` in `tools/sim/`, fonts matched to `sdkconfig`), renders into an RGB565 framebuffer and dumps a PNG. ESP-IDF headers are stubbed in `tools/sim/shim/` and device-only symbols in `tools/sim/stubs.c`. To preview a new screen: add its `.c` to `add_executable` in `tools/sim/CMakeLists.txt`, call its `*_screen_create()` from `tools/sim/main.c`, and add stubs for whatever it links against.

## What's Not Yet Done

- NBA API: box-score detail view, cursor pagination (`meta.next_cursor`, now followed inside each date chunk) and team logos (baked bitmap table, `team_logos.c`) are done
- Multi-league: tabs + ESPN college feed (MBB/WBB) + D1 logos are done and device-verified; WNBA *scores* have no feed yet (logos are baked, the tab row is NBA|MBB|WBB only)
- NVS persistence: brightness across reboots (slider saves on change but may not load on boot)

## Lessons Learned

- **LVGL timer**: `lv_timer_create()` creates an infinite-repeat timer by default (`repeat_cnt=0`). Use `lv_timer_set_repeat_count(timer, 1)` for one-shot timers. The `lv_timer_t` struct is opaque — don't access members directly.
- **NVS validation**: Always validate calibration data ranges (0-4095 for XPT2046) before trusting the `cal_saved` flag. Corrupt NVS data can cause inverted touch mapping, making buttons auto-fire on wrong screens.
- **Flash and NVS**: Using `esptool merge-bin` may not erase the NVS partition. If calibration data is corrupt, run `idf.py erase-flash` before flashing, or rely on the built-in validation that resets corrupt data to defaults.
- **Backlight polarity**: CYD2USB backlight is **active-low** (GPIO LOW = ON). `lcd_brightness_set(brightness)` must invert: `duty = LEDC_DUTY_MAX - brightness`. Without inversion, higher brightness values make the screen darker.
- **LVGL screen padding**: the default theme gives screens/containers `card` style `pad_all ≈ 13px`. It shrinks `LV_PCT(100)` sizes and offsets `set_pos`/`align` (e.g. keyboard `BOTTOM_MID` floats 13px up). Always `lv_obj_set_style_pad_all(scr, 0, 0)` on every screen. Same trap on **small fixed-height row containers** (e.g. 22px list rows): padding exceeds the height → negative content box → flex centering pushes children *outside* the row (they render over neighbouring widgets). Zero `pad_all` on every `lv_obj_create` you use as a row.
- **LVGL flex centering**: `lv_obj_set_flex_align(obj, main, cross, track)` — the 3rd arg places the *track* (a column track is as wide as its widest child). `CENTER` for cross alone is not enough: the track itself stays at the left edge. Use `LV_FLEX_ALIGN_CENTER` for both cross and track.
- **`LV_PCT(100) - N` is a trap**: it subtracts N from the fixed-point percent, not pixels (`LV_PCT(100)-40` = 234px, not 200). Use `LV_VER_RES - N` instead.
- **esp_lvgl_port thread safety**: the LVGL port task renders continuously and holds a mutex. **Any** LVGL call from another task (creating screens in `app_main`, `lv_timer_create/del` or widget updates from `esp_event`/backend task callbacks) must be wrapped in `lvgl_port_lock(0)`/`lvgl_port_unlock()`. Unserialized access causes intermittent `LoadProhibited` panics deep inside LVGL (observed in `lv_event_send` during screen creation — timing-dependent, may not crash for many boots). The port mutex is **not recursive**: never take it from LVGL task context (timer/event callbacks already run with it held).
- **Scroll room for the keyboard**: a scrollable container's `pad_bottom` extends its scroll range (via `lv_obj_get_self_height`). The wifi screen sets `pad_bottom = UI_KEYBOARD_H` on the content column so any focused field can scroll fully above the keyboard (`textarea_focus_cb` computes the minimal scroll).
- **ESP-IDF 6 http client**: event handler signature is `esp_err_t fn(esp_http_client_event_t *evt)` — no separate `client` arg (it's `evt->client`, and `evt->user_data` comes from `config.user_data`). `esp_http_client_get_last_errno()` was renamed `esp_http_client_get_errno()`. `esp_crt_bundle_attach` needs `#include "esp_crt_bundle.h"`.
- **HTTPS cert chains ending at a cross-signed root** (e.g. balldontlie.io's GTS cert served under a GlobalSign cross-cert) fail with `-0x3000` unless `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y` (set in `sdkconfig.defaults`).
- **esp_http_client treats 4xx as errors** (`perform()` returns non-OK) — but headers/status/body are still delivered. For probes where the response *content* is what matters (e.g. the `Date` header in `time_sync.c`), check `get_status_code()`/captured headers instead of `perform()`'s return code.
- **cJSON memory budget on this board**: the cJSON tree costs ~3-4x the JSON text size. Free heap during HTTP is ~95KB, so cap balldontlie responses (`per_page=15` ≈ 13KB text) — a 30-game page OOM'd mid-parse. Parse-failure diagnostics: `cJSON_GetErrorPtr()` offset into the body reveals truncation-vs-OOM.
- **balldontlie date queries**: `start_date`/`end_date` are inclusive; `season_type` omitted means *all non-preseason types* (October queries return 0 games until the `preseason` fallback runs). Each game carries quarter-by-quarter stats (~880B/game) — that's why `per_page` stays small.
- **balldontlie pages by game id, not by date, and ignores `sort`/`order`**: `next_cursor` means "games with a bigger id", and ids do not track game dates (a game added late to the schedule, e.g. a reschedule, gets a big id — Oct 14 2026 had id 22093897 while Oct 16 games were 2182619x). So a cursor page is never "the newest games", and a *partial* cursor walk leaves holes you can never fill. `nba_service` therefore fetches whole date chunks (adaptive width, 1–7 days, following `next_cursor` until the chunk is consumed) and sorts by `datetime` (ISO 8601 UTC — the string compares chronologically) newest-first.
- **The free balldontlie tier allows ~5 requests per minute**, and a chunk fetch costs 1-3 HTTPS calls (cursor pages + the preseason fallback on an empty chunk). `nba_api` therefore paces itself (`NBA_API_MIN_GAP_MS`, 13 s; `nba_api_note_request()` lets the clock-sync probe count too) and `nba_service` adapts the chunk width to cost-per-request (`svc_note_chunk_yield`). Any retry path must also be gated: an early version clamped a failed page back and re-requested it every cycle, which hammered the API (37 × 429) and, worse, reset the cache on the way, blanking a board that still had good data. Now: `s_fail_streak` drives a 15 s → 5 min backoff, a failed page move renders the nearest *cached* page, and re-anchoring on recent games only rewrites the cache after a successful fetch.
- **balldontlie dates are US game dates, the ESP32 clock is UTC**: games tip off in the US evening, which is already the next UTC day, so `time(NULL)`-based day maths labels tonight's games a day early. `time_sync_game_day_epoch()` anchors day 0 at `now - 12h` so the pager's "today" matches what the API reports.
- **Recolouring a button does not reach its child label** if the label got its own `lv_obj_set_style_text_color()`: child styles win over inherited ones. To dim a disabled `◀`/`▶`, set the colour on the *button* and leave the label unstyled (`cards_screen.c: make_page_button`).
- **HTTPS connections creep the heap (~200 B each)**: measured over 8 refresh cycles with the sample taken a full interval *after* the connection closed (so lwIP's deferred teardown is not blamed), free heap fell 98008 → 96808. Our own allocations balance; the loss is inside `esp_http_client`/esp-tls. Disabling `CONFIG_MBEDTLS_CLIENT_SSL_SESSION_TICKETS` did **not** help. Keep one connection per refresh (hence the `season_type` memory) and let `nba_service` reboot below `SVC_MIN_FREE_HEAP` (25 KB) — at 15-min refresh that is roughly one reboot per week.
- **Host UI preview (`tools/sim/`) gotchas**: (a) `lv_conf_template.h` wraps its whole body in `#if 0` — the copied `tools/sim/lv_conf.h` must flip it to `#if 1`, otherwise every define is ignored and LVGL silently compiles with defaults (missing fonts). (b) `LV_HOR_RES`/`LV_VER_RES` are **runtime** macros (`lv_display_get_horizontal_resolution(lv_display_get_default())`); using them in `lv_display_create()` before a display exists gives a 0x0 display and an endless refresh loop — use literal 320/240 in the harness. (c) `lv_conf_internal.h` prints `#pragma message("Possible failure to include lv_conf.h")` when the config isn't picked up.
- **LVGL built-in fonts are ASCII + `LV_SYMBOL_*` only**: typographic characters (em dash `—`, curly quotes) render as empty boxes. Keep on-screen strings to ASCII.
- **Absolute placement inside a flex container**: add `LV_OBJ_FLAG_FLOATING` to the child (the date/status labels on a score card) — otherwise it joins the layout and shifts its siblings.
- **Watch macro parameter names against struct members**: `#define G(date, ...)` used in `{ .date = date }` rewrites the *member name* too (`.​"2026-10-14" = ...`). Prefix parameters (`d`, `stat`, `tm`).
- **Header row needs `pad_all = 0` too**: the theme's ~13px padding on the 320px header eats 26px, and a `flex_grow(1)` status label then wraps to two lines mid-word.
- **`xQueueReceive(q, NULL, timeout)` asserts** (`queue.c: (!(pvBuffer == NULL && pxQueue->uxItemSize != 0))`) when the queue holds non-zero-size items — it took down the score-feed worker in a reboot loop. For "wake me up" signalling use a binary semaphore (`xSemaphoreCreateBinary` + `xSemaphoreGive`/`xSemaphoreTake` with the timeout), not a 1-byte queue.
- **Render memory**: LVGL widgets are allocated from LVGL's own pool (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`), so `esp_get_free_heap_size()` looks unchanged after building 15 cards. Log `lv_mem_monitor()` (`used_pct`/`frag_pct`) alongside it to see real render headroom.
- **`LV_LABEL_LONG_DOT` needs a bounded height**: on a label with `LV_SIZE_CONTENT` height the text *wraps and grows* instead of truncating. Single-line cells (the box-score table's team names) get an explicit height (~18px for 12pt) to get the `…` clip.
- **Never delete a widget from its own event callback**: the card tap that opens the detail view cannot `lv_obj_clean()` the container holding the card LVGL is still sending the event to. Copy the payload into a static, then defer one tick with a one-shot `lv_timer_create(cb, 5, NULL)` + `lv_timer_set_repeat_count(t, 1)` (LVGL frees a finished one-shot by itself).
- **Freeing a whole screen from a button on that screen**: `lv_scr_load(other)` first, then `lv_obj_delete_async(old_scr)` (LVGL 9, `lv_obj_tree.h`) — the delete runs on the next timer pass, out of the event. The detail view tears itself down this way on the back tap; keeping it alive instead cost 14 points of LVGL pool in the steady state (71% vs 57%). Remember to NULL the cached widget pointers.
- **`LV_EVENT_SCREEN_LOADED` is sent to the screen object** (`lv_obj_send_event(scr, …)`), so a screen can react to being shown: the cards screen asks `nba_service_rerender()` for its page back whenever it loads with an empty container. Pair it with `cards_screen_page_visible()` so the feed never builds a page of cards inside a hidden screen.
- **`snprintf`ing API struct fields needs explicit precision** (`"%.8s @ %.8s"`, `cell[16]` for `%d`): `-Werror=format-truncation` under `-O2` treats `char abbr[8]` + `%s` as unbounded and refuses the build.
- **balldontlie `team_id` is NOT `cdn.nba.com_id - 1610612736`**: only ATL/BOS line up; e.g. balldontlie 3 = BKN but 1610612739 = CLE (BKN's NBA.com id is 1610612751). `tools/make_team_logos.py` keeps an explicit abbr→CDN-id table and emits a labelled contact sheet — check it by eye instead of trusting any arithmetic.
- **Hand-packed LVGL images**: RGB565A8 = RGB565 plane (little-endian, `stride = w*2`) immediately followed by the A8 mask (`w*h` bytes), descriptor `.cf = LV_COLOR_FORMAT_RGB565A8, .data_size = color + alpha`. `tools/make_team_logos.py` implements the packing; validate against LVGL's `_calculate_draw_buf_size` before rendering blind.
- **Never pass an out-param and read it in the same call**: `lookup(table(&n), n, id)` is legal C but the arg evaluation order is unspecified — gcc/x86 read the garbage `n` *before* filling it (every logo silently vanished in the sim), xtensa happened to work. Sequence the calls in separate statements. Found by watching LVGL pool drop exactly one-card-per-image-worth in `tools/sim/` output — always diff the pool numbers after touching render-path code.
- **balldontlie/UA quirks (PC-side)**: the balldontlie edge 404s `/wnba/*` for unknown User-Agents (browser UA works, and the WNBA `teams` endpoint is intermittently flaky even so — generator carries a baked roster fallback); ESPN's CDN image endpoints 404 on Python's default `Python-urllib` UA. Both only matter for `tools/make_team_logos.py`, the ESP32 client is unaffected. (The ESPN *scoreboard* API wants a browser `User-Agent` too — `espn_api.c` sends `Mozilla/5.0`.)
- **Hand-written JSON scanners: clear "last token was a value" flags when a container opens**. A `':'` sets "next token is a value"; `"events":` then `[`/`{` never delivers that scalar, so the flag leaked and the *first key* of every object was consumed as a value — all later assignments keyed off the seeded container name. Symptom: near-total data loss with a few fields (second keys) surviving, which looks like depth-tracking bugs. `open_container` must reset the flag. And **test the parser on the host against real captured payloads first** (`tools/sim/run.sh` → `espnparse`): the same fixture run caught this plus two ESPN-shape traps below before a single byte was flashed.
- **ESPN scoreboard pre-seeds games it hasn't played**: `"score":"0"`, `"period":0`, a `"0:00"` displayClock and `"TBD"` shortDetail on scheduled events. Unless the parser forces score/linescores to -1 outside `in`/`final` (and prefers the tip text over the zeroed clock), the board shows "0 - 0" for tomorrow's games. Also: ESPN lists a 69-game November slate **oldest-tip-first**, so capping at N games by append order keeps the *wrong* half — keep the newest N via capped sorted insertion.
- **CMake `if(NOT X STREQUAL "")` is TRUE for an *undefined* X** (the bare name compares as its own literal), so the guard emitted `-DCYD_TEST_DAY_SHIFT=()` for plain builds. Test-hook guards must be `if(DEFINED X AND NOT X STREQUAL "")`. Related: **idf.py keeps `-D` options in the CMake cache** — after a `-DCYD_TEST_LEAGUE=2` soak, the next plain `idf.py build` still flashes the hooks; clear them with the empty form `idf.py -DCYD_TEST_LEAGUE= -DCYD_TEST_DAY_SHIFT= build`.
- **Header widgets eat LVGL pool faster than expected**: an `lv_btn` is two widgets (button + child label), and *each* `lv_obj_set_style_*` call allocates a per-object style property. 3 button-tabs were ~5 pool points and pushed the 12-card stress board past 100% (render hang at 99%, no crash line). The tabs are now clickable **labels** sharing one static `lv_style_t` (~2 points, 8-card board back at ~70%). Gotcha: `lv_style_set_*` has **no state selector** — pressed/hover colors go on `lv_obj_add_style(obj, &style, LV_STATE_PRESSED)` or a local property; and local properties outrank added styles, so an active-tab local bg also beats a shared PRESSED bg (no pressed flash).
- **A `-Wstringop-truncation` wrapper is still a wrapper**: even a `copy_str()` helper ending in `strncpy(dst,src,n-1); dst[n-1]=0;` gets flagged from `-O2 -Werror` when the source array is longer than the dest. Implement the helper with `strnlen(dst_len-1)` + `memcpy` + explicit NUL — deliberate truncation gcc cannot misread.
