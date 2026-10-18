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
| Header (`NBA \| WNBA \| MBB \| WBB` tabs + Wi-Fi badge (ᱤ bitmap when connected) + refresh + gear) | 50 px (`UI_HEADER_H_CARDS`) |
| Date pager (`<  Oct 15 …… 4/8  >`) | 26 px (`UI_PAGEBAR_H`) |
| Card list | 164 px (`LV_VER_RES - UI_HEADER_H_CARDS - UI_PAGEBAR_H`) |

Card list: flex column, 300 x 46 px cards, 8 px row gap (`UI_CARD_*`) — ~3.3 cards visible, the rest scrolls.

Pager: a page holds a fixed number of games (8 by default, `CYD_NBA_PAGE_SIZE`), newest first — a busy slate spills over several pages, a quiet stretch pulls several days into one, and the strip shows the date range it covers. `<` loads older games, `>` goes back to newer ones. The feed keeps the last 4 pages in RAM, so paging inside that window is instant and free; stepping past it fetches one more chunk of days. The arrow at the end of the range is greyed out.

League tabs (`NBA | WNBA | MBB | WBB`): the lit tab is the feed on screen. Tapping one swaps the whole board — the cache and pager reset, the header re-anchors on the new league's "today", and the service refetches from that league's source (balldontlie for the NBA and WNBA, ESPN for college).

Card anatomy: the main row `[logo] TEAM_A  102 - 98  TEAM_B [logo]` is **left-aligned and top-aligned** (logos ~22 px, abbrevs 16pt, score 20pt) at the same y as the game date in 12pt at the top-right — that is what lets the card be 46 px instead of 56. The centre text is capped at 100 px (`UI_CARD_CENTER_W`; strings over 12 chars step to 16pt and get `…`), so even the widest card clears the date. Along the bottom: full team names in 12pt (away left, home right) with the live game clock (`Q2 05:31`, `Halftime`) green in the middle; names clip with `…`. Away team first (broadcast convention). Winner's abbreviation and name are white, loser's muted; live scores are green. Logos come from baked flash tables (30 NBA + 13 WNBA + 182 college conference schools; see Team Logos below); a team without a baked bitmap draws a placeholder ring instead of skipping its logo. Scheduled college games show ESPN's tip text (`11/10 - 6:30 PM EST`) in the centre slot.

Tap a card → game detail view: header `AWAY @ HOME` + status (`Final`, `Q2 05:31`), the score big with both logos flanking it, a box-score table (`Q1 Q2 Q3 Q4 [OT] T` with full team names — the NBA plays quarters, MBB plays halves so its table reads `H1 H2 [OT] T`, WBB quarters), the game date + tip-off, and while a game runs the timeouts left (NBA only — ESPN's scoreboard doesn't carry them). balldontlie's WNBA scoreboard has no per-period stats, so the table hides entirely for WNBA games and the big score line carries the story. Needs no extra API call — the games list already carries the period scores, and `GET /games/{id}` returns the same object. `◀` returns to the board.

Memory budget: LVGL widgets live in LVGL's own pool (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`), measured on device — ~70% with a full page of 8 score cards (each card ≈ 3.2KB: 7 labels + 2 name labels + 2 logo widgets; the logo pixels themselves stay in flash; the header tabs cost ~1 point). Keep an eye on it when adding cards/widgets (`Rendered N card(s): lv pool …` in the serial log); with the tabs in the header, `idf.py -DCYD_NBA_PAGE_SIZE=12` now sits at ~99% of the pool in the sim and is out of reach — 8 is the hard ceiling. A page of cards (~25KB) and the detail view (~13KB) do not sit side by side: tapping a card drops the page (measured 55% with the detail view open), the feed skips re-rendering into a hidden screen, the page is rebuilt from the cache when the board comes back, and the detail view frees itself on the way back — so the board sits at the same ~70% long after the first visit (keeping it alive cost 14 points). Fetched games sit in static arrays outside that pool (~17KB for the 4-page cache plus one API page), and the logo bitmaps (~315KB packed — the 182-school NCAA table is ~256KB of it) are all linked into the binary: **~1.74MB of the 2MB app partition, ~17% free**. Flash is the scarce resource now — measure before baking any more art. The worker also logs `cycle start, free heap: N` each refresh and reboots below 25 KB, because ESP-IDF's HTTPS stack keeps a couple of hundred bytes per connection.

API budget: the free balldontlie tier allows about **5 requests per minute**, so `nba_api` paces itself (13 s between calls, the clock-sync probe counts too) and failures back off 15 s → 5 min instead of retrying in a loop. Fetches ask for whole date *chunks* whose width adapts to how busy the slate is (1 day when a chunk costs two requests, up to 7 days when it stays thin), which keeps a page step at roughly one HTTPS call in either season. Paging inside the 4 cached pages costs nothing at all. College feeds ride ESPN's public scoreboard (no key, no published limit — be polite anyway): one request per calendar day, its own 4 s pacing, and each day keeps only its **24 newest** games (a November slate can list 60+; ESPN orders them oldest-tip-first, so keeping the first N would drop the evening games).

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
│   ├── cards_screen.c/.h   # scores card list + NBA/MBB/WBB league tabs
│   ├── game_detail_screen.c/.h # tap a card: box score by period + team names
│   ├── team_logos.c/.h    # generated NBA+WNBA+NCAA logo bitmaps (flash) + id lookups
│   ├── league_logos.c/.h  # game_logo(league,id) dispatcher over the logo tables
│   ├── nba_api.c/.h        # BallDontLie NBA API client (games by date range)
│   ├── espn_api.c/.h       # ESPN scoreboard client (MBB/WBB): constant-memory streaming JSON parser
│   ├── nba_service.c/.h    # Score feed worker: fetch one day page + push to cards screen
│   ├── time_sync.c/.h      # System clock sync from HTTPS Date header
│   ├── basketball_img.c/.h # LVGL image descriptor (64x64)
│   └── ...more screens
tools/
├── basketball.png          # Source image
├── convert_image.sh        # LVGL image converter script
├── make_wifi_glyph.py      # regenerates main/wifi_glyph_img.c (connected-badge mark bitmap)
├── make_team_logos.py      # regenerates main/team_logos.c (fetch + rasterise + pack)
├── logos/                  # generated cache (SVG sources, contact sheet) — gitignored
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

### Team Logos

The NBA + WNBA + NCAA D1 team logos are baked into flash as RGB565A8 bitmaps
(`main/team_logos.c`, generated — do not edit). To regenerate (needs `pillow` + `cairosvg`):

```bash
python3 tools/make_team_logos.py                 # all leagues, 22 px, ~315 KB of bitmaps
python3 tools/make_team_logos.py --league nba    # one league (nba|wnba|ncaa|all)
python3 tools/make_team_logos.py --all-conferences  # bake all 362 D1 schools
```

NBA art comes from `cdn.nba.com` SVGs (ESPN nba PNGs as fallback), WNBA and
NCAA from `a.espncdn.com/i/teamlogos/{wnba,ncaa}/500/<espn_team_id>.png` (the
NBA CDN has no WNBA or college tree). Id tables come from balldontlie's
`/nba/v1/teams` and `/wnba/v1/teams` (the WNBA endpoint 404s intermittently —
the script falls back to a baked 13-team roster); the D1 school list comes from
ESPN's own leagues endpoint (`teams_ncaa.json` in the cache). By default the
NCAA set is trimmed to the **power-five + group-of-five core conferences**
(membership comes from one ESPN standings call, cached as
`ncaa_conferences.json` — it refreshes itself every season); schools outside
that set draw the cards-screen placeholder ring. `--all-conferences` bakes all
362.
The script also writes `tools/logos/contact_sheet.png` — check it by eye before
trusting an id mapping (balldontlie's team ids are **not** linearly related to
the NBA.com ids in the CDN URL, and ESPN school ids are not guessable either,
hence the tables in the script). Downloads are cached under `tools/logos/`.

Footprint: 30 NBA ~44 KB + 13 WNBA ~18 KB + 182 NCAA schools ~256 KB ≈ 315 KB
of bitmaps in the code segment — ~15% of the app partition, leaving the binary
at ~87% (the untrimmed 362-school set costs ~569 KB and ~96%). All three
tables are live now (the college feed links the NCAA set; MBB and WBB share ONE
school set — ESPN serves the same logo for both genders of a school). Teams
without a baked bitmap get a runtime placeholder ring on the card, so trimming
costs no layout — just the logo art.

### UI Preview (no hardware needed)

`tools/sim/` compiles the real screen sources against host LVGL and renders a
320x240 PNG, so layout changes can be checked without flashing:

```bash
tools/sim/run.sh          # -> /tmp/opencode/cards_<scenario>.png for every scenario
```

Mock game records (final / live / scheduled / postponed, NBA + an MBB college
slate) live in `tools/sim/main.c`. `run.sh` also runs the ESPN scoreboard
parser over real captured API bodies (`tools/sim/fixtures/espn_*.json` — one
busy final day per league plus a 69-event November scheduled day): the exact
streaming scanner the device uses, with assertions on halves-vs-quarters,
OT linescores, the scheduled "0-0" guard and the newest-24 cap. If you touch
`espn_api.c`, that test must stay green — it caught three device-visible bugs
before they ever got flashed. To preview another screen, add its `.c` to the
target list in `tools/sim/CMakeLists.txt` and stub whatever ESP-IDF symbols it
needs (see `tools/sim/shim/` and `tools/sim/stubs.c`).

## Current State

- **Boot flow**: On first boot (no NVS calibration) → calibration screen. After calibration → splash (3s) → cards
- **Touch**: XPT2046 calibrated, LVGL indev working, linear coordinate mapping
- **Calibration**: 4-corner crosshair calibration with long-press undo, quadrant validation, debounce
- **NVS**: Calibration data + saved flag persisted across reboots, validated on load (range 0-4095)
- **Settings**: Brightness slider (saves to NVS), Wi-Fi button, Touch Calibration button
- **Wi-Fi**: Scan, connect, saved networks (NVS), boot auto-connect, status badge — all working (while connected the badge shows the ᱤ mark, U+1C64, baked as an RGB565A8 bitmap in `wifi_glyph_img.c` — the built-in Montserrat sets carry only ASCII + `LV_SYMBOL_*`; regenerate with `python3 tools/make_wifi_glyph.py`)
- **NBA API**: [BallDontLie](https://www.balldontlie.io/) client (`nba_api.c`) — HTTPS + cJSON, date-range game queries; clock synced from the HTTPS `Date` header (`time_sync.c`)
- **WNBA scores**: balldontlie's `/wnba/v1/games` on the same free key — `nba_api` URL helpers (`nba_api_url_nba()` / `nba_api_url_wnba()`) plus a `parse_game` branch for its payload quirks (`date` doubles as the UTC `datetime`, scores are top-level `away_score`/`home_score`, `status_state` carries the readable state, **no per-period stats** — the detail view hides the box-score table when every period slot is -1). The `season_type`/preseason dance is NBA-only. Device-verified on live playoff games (`-DCYD_TEST_LEAGUE=1`)
- **College feed (MBB/WBB)**: ESPN's public scoreboard API (`espn_api.c`) — no key, browser `User-Agent` required. **Coverage caveat:** it lists only the network-covered D1 games — ~10-19/day in conference season, 60+ on a November axis day — it is *not* the full ~350-game D1 slate (balldontlie's college endpoints exist but paywall the games). Since the body pads every event to ~20 KB (200-400 KB/day), the client never buffers or cJSON-parses it: a hand-written constant-memory streaming scanner (`espn_parse_scoreboard`) walks the JSON once and keeps each day's **24 newest** games; it is regression-tested host-side against captured fixtures
- **League tabs**: the header is `NBA | WNBA | MBB | WBB` — the lit tab is the live feed; tapping swaps the service's league, clears cache+pager and re-anchors (the worker, not the LVGL callback, redraws — the port mutex is not recursive). `nba_game_t` carries a `league_t`, and `game_logo(league, id)` picks the right baked table
- **Score cards**: `nba_service.c` worker fetches games on first Wi-Fi connect and every 15 minutes (on page 1 that re-anchors on today, so starting games and new slates come in), remembering which `season_type` answered so a refresh usually costs one HTTPS call per date chunk (October needs `preseason`); the cards screen renders one box per game (`AWAY  102 - 98  HOME` left-aligned, date top-right, live clock bottom-centre) and has a refresh button in the header
- **Paging**: the strip under the header (`<  Oct 14 - Oct 16  page 3  >`) steps through whole pages of games (8 each). balldontlie pages by *game id*, and ids do not track dates, so older pages are reached by moving the date window backwards (one day-chunk at a time, following `next_cursor` until the chunk is consumed), not by the cursor. Four pages stay in RAM: paging inside them is instant, stepping past the older edge fetches another chunk, and walking back off the newest edge re-anchors on today's games
- **Game detail**: tapping a card opens `game_detail_screen.c` — box score by period (OT column only when the game went to OT), full team names, game date + tip-off (labelled UTC, since `date` is the US game date and `datetime` is UTC), and timeouts left while a game is live. The headline score is flanked by both team logos. Its widgets are built on the first tap, and the card page is dropped while it is open (see Memory budget)
- **Team logos**: 30 NBA + 13 WNBA + 182 conference-school NCAA D1 logos baked into flash as ~22 px RGB565A8 bitmaps (`team_logos.c`, generated by `tools/make_team_logos.py`); rendered inline on the score cards and in the detail view's score line. balldontlie NBA and WNBA ids are **separate overlapping namespaces** (both start at 1), and ESPN school ids are a third namespace, so there is one accessor per league (`team_logo` / `wnba_team_logo` / `ncaa_team_logo`) behind a `game_logo(league, id)` dispatcher — pick by the feed the game came from, never by the number. MBB and WBB share the single NCAA school table. ~44 KB + 18 KB + 256 KB of flash, ~600 B of LVGL pool per card, zero HTTP; schools outside the baked set draw a placeholder ring
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
                                                                    ├── [NBA|MBB|WBB tabs] → swap the feed (cache+pager reset)
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
