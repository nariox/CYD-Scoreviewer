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
| Header (`NBA \| WNBA \| NCAA \| NCAAW` tabs + Wi-Fi badge (native `LV_SYMBOL_WIFI`: black = connected, `LV_SYMBOL_CLOSE` cut over it = off, blinking grey = connecting) + refresh + gear) | 50 px (`UI_HEADER_H_CARDS`) |
| Date pager (`<  Oct 15 …… 4/8  >`) | 26 px (`UI_PAGEBAR_H`) |
| Card list | 164 px (`LV_VER_RES - UI_HEADER_H_CARDS - UI_PAGEBAR_H`) |

Card list: flex column, 300 x 46 px cards, 8 px row gap (`UI_CARD_*`) — a page is exactly 3 cards (154 px inside the 164 px band), so it never scrolls.

Pager: a page holds a fixed number of games (3 by default, `CYD_NBA_PAGE_SIZE`), newest first — a busy slate spills over several pages, a quiet stretch pulls several days into one, and the strip shows the date range it covers. `<` loads older games, `>` goes back to newer ones. The feed keeps the last 4 pages in RAM, so paging inside that window is instant and free; stepping past it fetches one more chunk of days. The arrow at the end of the range is greyed out. While any fetch is in flight (page step, refresh button, cold-league fill) a small white comet spins in the pager strip between the page counter and `>` — the board may still show the previous page for a moment, but the spinner is the answer to "is it hung or loading?". It hides the instant the cycle renders, and a hidden LVGL object costs nothing.

League tabs (`NBA | WNBA | NCAA | NCAAW` — the men's game as "NCAA", the women's as "NCAAW"): the lit tab is the feed on screen. Each tab is sized from `lv_text_get_size()` for its own label + `UI_TAB_PADX` (a uniform box for "NCAAW" would overflow the header). Tapping one swaps the whole board — the tap lights the label immediately and only *queues* the switch; the feed worker applies it at a cycle boundary (cache and pager reset, header re-anchors on the new league's "today") and aborts whatever fetch was in flight at its next request, so the board never mixes two leagues and a tap lands in seconds even if a refresh was mid-flight. A tab that has been shown once this session then **paints instantly** from its RAM *page box* (four boxes, up to 12 games — four pages — each, refreshed on every successful cycle and stashed again whenever you leave the tab; the restore also lands on the page you left that tab on); the real refill follows quietly in the background. Only a cold tab (first visit since boot, before the boot pre-warm reaches it) waits on its league's source — and that source is ESPN for all four leagues now (balldontlie is retired).

Card anatomy: the main row `[logo] TEAM_A  102 - 98  TEAM_B [logo]` is **left-aligned and top-aligned** (logos ~22 px, abbrevs 16pt, score 20pt) at the same y as the game date in 12pt at the top-right — that is what lets the card be 46 px instead of 56. The centre text is capped at 100 px (`UI_CARD_CENTER_W`; strings over 12 chars step to 16pt and get `…`), so even the widest card clears the date. Along the bottom: full team names in 12pt (away left, home right) with the live game clock (`Q2 05:31`, `Halftime`) green in the middle; names clip with `…`. Away team first (broadcast convention), and the home abbreviation carries a `*` — its arena is where the game is played (no venue text fits a 46 px card; the full name is on the detail view). Winner's abbreviation and name are white, loser's muted; live scores are green. Logos come from baked flash tables (30 NBA + 13 WNBA + 182 college conference schools; see Team Logos below); a team without a baked bitmap draws a placeholder ring instead of skipping its logo. Scheduled college games show ESPN's tip text (`11/10 - 6:30 PM EST`) in the centre slot.

Tap a card → game detail view: header `AWAY @ HOME` + status (`Final`, `Q2 05:31`), the score big with both logos flanking it, a box-score table (`Q1 Q2 Q3 Q4 [OT] T` with full team names — the NBA plays quarters, MBB plays halves so its table reads `H1 H2 [OT] T`, WBB quarters), the game date + tip-off, the venue, and while a game runs the timeouts left (NBA only — ESPN's scoreboard doesn't carry them). The venue line reads the arena straight from the ESPN scoreboard — every league's scoreboard names its event's venue (exact even at neutral sites) — and falls back to the baked home-arena table (see Team Venues below) when a stored game predates it; it hides when neither knows. ESPN carries per-period stats for every league, so the box-score table renders everywhere (it still hides for any payload without them). Needs no extra API call — the scoreboard response already carries the period scores. `◀` returns to the board.

Memory budget: LVGL widgets live in LVGL's own pool (`CONFIG_LV_MEM_SIZE_KILOBYTES=48`), measured on device — **75%** with a full 3-card page (each card ≈ 3.2KB: 7 labels + 2 name labels + 2 logo widgets; the logo pixels themselves stay in flash; the header tabs cost ~1 point, the pager busy-spinner ~750 B). Keep an eye on it when adding cards/widgets (`Rendered N card(s): lv pool …` in the serial log) — and 48 KB is the diet floor too: the boot screens alone claim ~26.7 KB, and a 32 K pool hung the first render (caught in the sim). A page of cards (~10KB) and the detail view (~13KB) do not sit side by side: tapping a card drops the page, the feed skips re-rendering into a hidden screen, the page is rebuilt from the cache when the board comes back, and the detail view frees itself on the way back. Fetched games sit in static arrays outside that pool — the 4-page cache (12 games), four 12-game page boxes and one 24-game parse page. The 3-card shape is what pays for the **44 KB mbedTLS static arena** (`.bss`, ON by default): handshakes draw from the arena instead of the system allocator, so the cert-storm failure band (~45.5 K free heap with the stock allocator) is gone — device-proven 100/100 alternating ESPN/Google handshakes green at 14,360 B free heap (`tools/mbedtls_opt/README.md`). The pool at the arena shape boots around 37.7 K; the stock 46 K storm floor and 48 K pre-warm gate are gated on `mbedtls_arena_active()` (both would read as permanent failures at this shape), and the 25 KB absolute floor stays load-bearing everywhere — the ~200 B/connection lwIP drift lives there and still self-heals via roughly one reboot per week. TLS record buffers are still the per-connection allocation: `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` must stay 16384 — 8192 passes handshakes but breaks the body read against Akamai's 16 KB records, 4096 breaks the handshake (documented in `sdkconfig.defaults`). The logo bitmaps (~315KB packed — the 182-school NCAA table is ~256KB of it) are all linked into the binary: **~1.74MB of the 2MB app partition, ~17% free**. Flash is the scarce resource now — measure before baking any more art. The worker also logs `cycle start, free heap: N` each refresh and reboots below 25 KB, because ESP-IDF's HTTPS stack keeps a couple of hundred bytes per connection.

API budget: all four leagues ride ESPN's public scoreboard (no key, no published limit — be polite anyway): one request per calendar day, `espn_api` paces itself at 4 s between calls, requests inside one burst share **one keep-alive TLS connection** (the handshake is the expensive part — the worker releases the connection when it goes idle, so a day-walk/pre-warm burst costs one handshake and the idle pool holds nothing), and failures back off 15 s → 5 min instead of retrying in a loop. Each day keeps only its **24 newest** games (a November college slate can list 60+; ESPN orders them oldest-tip-first, so keeping the first N would drop the evening games). Paging inside the 4 cached pages costs nothing at all; stepping past the older edge fetches one more day, and the pre-warm (fired on the first *connected* cycle, once per boot) spends ~10 background requests (plus the once-a-day slate hunt, and a weekly re-probe only for a league whose hunt came up clean-empty) to make every tab instant; a tab whose box is still recent is skipped. The clock sync is one unpaced HEAD probe per boot. The newest games of each **pro** league (12 games, ~4.4 KB) are snapshotted to NVS (`games_nba` / `games_wnba`): a reboot shows that board in ~8 s with zero API calls, and a reboot *inside* the refresh interval skips the first fetch entirely. College is not persisted — a 3rd/4th blob would crowd the 24 KB NVS partition, and the RAM page boxes already cover the session. balldontlie (free tier: ~5 requests/min, hence the old 13 s pacing and chunk-width calculus) is fully retired.

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
│   ├── cards_screen.c/.h   # scores card list + NBA/WNBA/NCAA/NCAAW league tabs
│   ├── game_detail_screen.c/.h # tap a card: box score by period + team names
│   ├── team_logos.c/.h    # generated NBA+WNBA+NCAA logo bitmaps (flash) + id lookups
│   ├── league_logos.c/.h  # game_logo(league,id) dispatcher over the logo tables
│   ├── team_venues.c/.h   # generated NBA+WNBA home arenas (flash), keyed by balldontlie id
│   ├── nba_api.h           # shared types ONLY (nba_game_t/league_t) — the balldontlie client is retired
│   ├── espn_api.c/.h       # ESPN scoreboard client (ALL four leagues): constant-memory streaming JSON parser
│   ├── pro_team_ids.c/.h   # generated ESPN→balldontlie pro team id tables (parse-boundary remap)
│   ├── nba_service.c/.h    # Score feed worker: fetch one day page + push to cards screen
│   ├── time_sync.c/.h      # System clock sync from HTTPS Date header
│   ├── basketball_img.c/.h # LVGL image descriptor (64x64)
│   └── ...more screens
tools/
├── basketball.png          # Source image
├── convert_image.sh        # LVGL image converter script
├── make_team_logos.py      # regenerates main/team_logos.c (fetch + rasterise + pack)
├── make_team_venues.py     # regenerates main/team_venues.c (harvest ESPN pro scoreboards)
├── make_pro_id_map.py      # regenerates main/pro_team_ids.c (ESPN scoreboard ids → balldontlie ids)
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
tables are live now (the college feed links the NCAA set; NCAAB and NCAAW share ONE
school set — ESPN serves the same logo for both genders of a school). Teams
without a baked bitmap get a runtime placeholder ring on the card, so trimming
costs no layout — just the logo art.

### Team Venues

The detail view shows where each game is played. Every ESPN scoreboard —
all four leagues — carries `competitions[].venue` per game: arena +
city/state, exact even at neutral sites — and the streaming parser stores it
in `nba_game_t.venue` (the `", City ST"` tail is appended only when it fits
whole; an arena name is never half-cut). The baked table below
(`main/team_venues.c`, generated — do not edit) is the **fallback** for
stored pro games whose snapshot predates the ESPN feed; college never needs
it:

```bash
python3 tools/make_team_venues.py            # offline if the harvest cache exists
python3 tools/make_team_venues.py --refresh  # re-walk ESPN's NBA/WNBA scoreboards
```

The tool walks ESPN's pro **scoreboards** day-by-day (the `/teams` endpoints
have no venue either) and keeps each home team's arena, cached at
`tools/logos/venues_harvest.json`. Teams are matched by **full name** —
ESPN's pro abbreviations differ from balldontlie's, and balldontlie's team
list also contains retired franchises whose names collide with live ones —
so the table is capped to the current namespace (NBA 1..30, WNBA 1..13) and
prints a warning for any hole inside it. Arenas get renamed by sponsorship;
`--refresh` picks up what ESPN says now. On the card list a `*` after the
home abbreviation marks the home team (whose building the game is in); the
full arena name is one tap away on the detail view.

### UI Preview (no hardware needed)

`tools/sim/` compiles the real screen sources against host LVGL and renders a
320x240 PNG, so layout changes can be checked without flashing:

```bash
tools/sim/run.sh          # -> /tmp/opencode/cards_<scenario>.png for every scenario
```

Mock game records (final / live / scheduled / postponed, NBA + a college
slate) live in `tools/sim/main.c`. `run.sh` also runs the ESPN scoreboard
parser over real captured API bodies (`tools/sim/fixtures/espn_*.json` — one
busy final day per league, pro fixtures included, plus a 69-event November
scheduled day): the exact streaming scanner the device uses, with assertions
on halves-vs-quarters, OT linescores, the scheduled "0-0" guard, the
newest-24 cap and the pro id remap (ESPN→balldontlie discriminators, plus
the unmapped-team → placeholder case). If you touch `espn_api.c`, that test
must stay green — it caught three device-visible bugs before they ever got
flashed. To preview another screen, add its `.c` to the
target list in `tools/sim/CMakeLists.txt` and stub whatever ESP-IDF symbols it
needs (see `tools/sim/shim/` and `tools/sim/stubs.c`).

## Current State

- **Boot flow**: On first boot (no NVS calibration) → calibration screen. After calibration → splash (3s) → cards. The splash carries a live stage line under the subtitle ("Connecting to Wi-Fi..." → "Showing stored scores..." / "Fetching first scores..."), driven by the Wi-Fi state callback and the feed worker while the splash is still on screen
- **Touch**: XPT2046 calibrated, LVGL indev working, linear coordinate mapping
- **Calibration**: 4-corner crosshair calibration with long-press undo, quadrant validation, debounce
- **NVS**: Calibration data + saved flag persisted across reboots, validated on load (range 0-4095), and the loaded calibration is **applied to the touch handler at boot** (it used to be loaded and dropped); brightness round-trips too
- **Settings**: Brightness slider (seeded from NVS on open, saved once on release — not per drag pixel), Wi-Fi button, Touch Calibration button
- **Wi-Fi**: Scan, connect, saved networks (NVS), boot auto-connect, status badge — all working (the badge is iconographic, built from the native `LV_SYMBOL_WIFI` glyph: black while connected; `LV_SYMBOL_CLOSE` — painted in the header colour, so it only "cuts" where it crosses the fan — when off; a greyed fan blinked by a 500 ms LVGL timer while connecting. No bitmaps, no extra flash; `tools/make_wifi_glyph.py` and `wifi_glyph_img.c` from the bitmap-pair era are retired)
- **Score feed (all four leagues)**: ESPN's public scoreboard API (`espn_api.c`) — `nba`, `wnba`, `mens`/`womens` college paths, no key, browser `User-Agent` required; balldontlie + cJSON are retired. **Coverage caveat (college):** the college scoreboard lists only the network-covered D1 games — ~10-19/day in conference season, 60+ on a November axis day — it is *not* the full ~350-game D1 slate (balldontlie's college endpoints exist but paywall the games). ESPN publishes each slate ~2 weeks out. Since the body pads every event to ~20 KB (200-400 KB/day), the client never buffers or tree-parses it: a hand-written constant-memory streaming scanner (`espn_parse_scoreboard`) walks the JSON once and keeps each day's **24 newest** games; it is regression-tested host-side against captured fixtures for all four leagues. **Off-season:** October has zero D1 slates — instead of a dead "No data" board the worker probes forward for the nearest upcoming slate (jump by a week up to +28 days, then walk back inside the bracket, ~9 requests, remembered per league in `s_slate_day[]`) and anchors the pager there, so the tab shows the opening exhibitions with their tip text; a clean-empty hunt becomes a weekly-retry verdict instead of re-probing every cycle (that's also what the WNBA tab lands on after its finals). **Preseason is covered** (all leagues): ESPN's scoreboard lists exhibition games like any other (the Nov 1-2 college opening alone has 180+ events; NBA preseason likewise)
- **WNBA scores**: ESPN's WNBA scoreboard on the same code path as every other league — per-period stats and venue included (balldontlie's WNBA payload, which had neither, is gone). The finals week rendered live on device; after them the tab paints its NVS blob (the last Finals page) while the slate hunt reports "no slate" weekly
- **Per-league page boxes + full pre-warm**: every tab keeps its newest games (up to `SVC_BOX_GAMES` = 12 = four pages) in a RAM *page box* (`s_page_box[4]`) so revisits paint instantly: each successful cycle refreshes the shown league's box, switching away stashes the leaving board's page one more time, and on the first *connected* cycle the worker pre-warms **all three** hidden tabs (~10 polite background requests — every league is ESPN now — plus the probed slate day so the refill needs no re-hunt; leagues with a recent box are skipped). `svc_cache_load` tries the box for any league first, then the NVS blob, and a box restore also snaps the pager back to **the page the viewer left that tab on** (`s_last_page[lg]`, clamped to the box's page count). Measured on device: tab switch → board in ~200 ms vs 20-40 s cold. RAM rather than NVS: a 3rd/4th ~4.4 KB blob would eat the 24 KB NVS partition's GC headroom, and "instant switch" is a session-scoped job
- **League tabs**: the header is `NBA | WNBA | NCAA | NCAAW` — the lit tab is the live feed; tapping lights the label and queues the switch: the worker (never the LVGL callback — the port mutex is not recursive, and the fetch loops read the feed state per request) applies the cache+pager reset at a cycle boundary and drops any in-flight fetch at its next request boundary, so boards never mix leagues. Each tab is sized from its own measured label (`lv_text_get_size` + `UI_TAB_PADX`): "NCAAW" (~49 px at 12pt) would push four uniform boxes past 320 px. `nba_game_t` carries a `league_t`, and `game_logo(league, id)` picks the right baked table
- **Score cards**: `nba_service.c` worker starts at boot (unconditional — with no reachable saved network it still paints the NVS snapshot board, and the Wi-Fi-connect callback wakes it out of backoff the moment any network lands) and fetches every 15 minutes (on page 1 that re-anchors on today, so starting games and new slates come in), walking ESPN one day per request backwards until the page fills; the cards screen renders one box per game (`AWAY  102 - 98  HOME*` left-aligned — the `*` marks the home team, date top-right, live clock bottom-centre) and has a refresh button in the header. After a successful, untouched cycle the worker **idle-prefetches** up to 3 more days into the cache so the next `<` renders from RAM
- **Paging**: the strip under the header (`<  Oct 14 - Oct 16  page 3  >`) steps through whole pages of games (3 each — a page never scrolls). Older pages are reached by walking ESPN's scoreboards backwards one day per request until the page is cached — no cursor anywhere in the ESPN model. Four pages stay in RAM: paging inside them is instant (device-measured ~270 ms median for a cached step), stepping past the older edge fetches another day, and walking back off the newest edge re-anchors on today's games. Reaching the edge of history renders the nearest cached page and is *not* treated as a fetch failure (no backoff)
- **Score cache across reboots**: the newest 12 games (~4.4 KB) of each **pro** league are snapshotted to NVS whenever they change (`games_nba` / `games_wnba`, validated header with a `SVC_BLOB_MAGIC` version + `sizeof(nba_game_t)`, and every game's `league` field checked against the feed on load — a snapshot holding another league's games or written by an older struct/feature level is dropped and rewritten), and loaded at worker start and on league-tab switches — the board shows the last fetched scores ~8 s after boot with zero API calls even when no network is reachable at all, and a boot inside the refresh interval skips the first fetch (within a session the RAM page box is tried first; the blob is the cross-reboot fallback). The ESPN migration kept the blob format deliberately compatible: balldontlie-era snapshots load as-is (same struct, same id namespace — the ESPN→balldontlie id remap happens at the parse boundary, before the blob), device-verified accepted. The magic was bumped once alongside the ghost filter so snapshots that still held `canceled` placeholder games self-reject. (A fill that succeeds also evicts cache games dated beyond the window it fetched — a `CYD_TEST_DAY_SHIFT` soak build once parked 4 real Nov 1 NBA games on a real-Oct-18 board's newest edge, and re-anchoring only walks backwards.) College is not persisted — a 3rd/4th blob would crowd the 24 KB NVS partition, and the RAM page boxes cover the session
- **Game detail**: tapping a card opens `game_detail_screen.c` — box score by period (OT column only when the game went to OT), full team names, game date + tip-off (labelled UTC, since `date` is the US game date and `datetime` is UTC), the venue, and timeouts left while a game is live. The headline score is flanked by both team logos. Its widgets are built on the first tap, and the card page is dropped while it is open (see Memory budget)
- **Team logos**: 30 NBA + 13 WNBA + 182 conference-school NCAA D1 logos baked into flash as ~22 px RGB565A8 bitmaps (`team_logos.c`, generated by `tools/make_team_logos.py`); rendered inline on the score cards and in the detail view's score line. balldontlie NBA and WNBA ids are **separate overlapping namespaces** (both start at 1), and ESPN school ids are a third namespace, so there is one accessor per league (`team_logo` / `wnba_team_logo` / `ncaa_team_logo`) behind a `game_logo(league, id)` dispatcher — pick by the feed the game came from, never by the number. (The ESPN pro feed's ids are translated into the balldontlie namespace at parse time by the generated `pro_team_ids.c`, so the tables keep speaking balldontlie ids — ESPN 17 is BKN, balldontlie 17 is POR.) NCAAB and NCAAW share the single NCAA school table. ~44 KB + 18 KB + 256 KB of flash, ~600 B of LVGL pool per card, zero HTTP; schools outside the baked set draw a placeholder ring
- **Venues**: the detail view names the arena — every league's ESPN scoreboard carries `competitions[].venue` per game (`→ nba_game_t.venue`, exact at neutral sites, `, City ST` only when the whole tail fits 48 chars); the baked home-arena table (`team_venues.c`, harvested from ESPN pro scoreboards by `tools/make_team_venues.py`) remains the fallback for stored pro games that predate it, keyed per league by balldontlie team id like the logos. Cards carry a `*` on the home abbreviation (the arena doesn't fit a 46 px card). Device-verified: Big East final shows PeoplesBank Arena (not UConn's gym); `-DCYD_NBA_AUTODETAIL=1` logs `opened game N @ <venue>` per open (an ESPN-parsed NBA game through the NVS blob round-trip: `game 401909090 @ Frost Bank Center, San Antonio TX`)
- **UI preview**: `tools/sim/` renders screens on the host (`tools/sim/run.sh`) — see "UI Preview" above

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
                                                                    ├── [NBA|WNBA|NCAA|NCAAW tabs] → swap the feed (cache+pager reset; NVS/RAM page if cached)
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
- **Boot-time LVGL calls need the port lock too**: `lv_theme_default_init()` and `lvgl_port_add_touch()` ran unlocked while the LVGL port task had been rendering since `lcd_init()` — a race that survived for months, then went deterministic on an unrelated binary-layout shift (WDT barking every 5 s, boot parked in `lv_inv_area`, no screen). Anything touching LVGL after `esp_lvgl_port_init()` must hold `lvgl_port_lock(0)` — full lesson list in AGENTS.md.
