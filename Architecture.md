# CYD Basketball Scores — Architecture

Design detail for the firmware: screen anatomy, the score-feed model, the memory and API budgets, and the generation tooling. The AI-agent brief (commands, hooks, constraints, lessons) lives in AGENTS.md; caveats and history in Notes.md.

## Layout cheat sheet (320x240)

| Band | Height |
|------|--------|
| Header (`NBA \| WNBA \| NCAA \| NCAAW` tabs + Wi-Fi badge (native `LV_SYMBOL_WIFI`: black = connected, `LV_SYMBOL_CLOSE` cut over it = off, blinking grey = connecting) + refresh + gear) | 40 px (`UI_HEADER_H_CARDS`; theme pass 1 trimmed 50→44, pass 2 →40) + 4 px `UI_HEADER_GAP` of bg before the pager |
| Date pager (`<  Oct 15 …… 4/8  >`) | 26 px (`UI_PAGEBAR_H`) |
| Card list | 170 px (`LV_VER_RES - UI_HEADER_H_CARDS - UI_HEADER_GAP - UI_PAGEBAR_H`) |

Card list: flex column, 300 x 46 px cards, 12 px row gap + 4 px top offset (`UI_CARD_*`) — a page is exactly 3 cards (162 px inside the 170 px band), so it never scrolls.

Pager: a page holds a fixed number of games (3 by default, `CYD_NBA_PAGE_SIZE`), newest first — a busy slate spills over several pages, a quiet stretch pulls several days into one, and the strip shows the date range it covers. `<` loads older games, `>` goes back to newer ones. The feed keeps the last 6 pages in RAM, so paging inside that window is instant and free; stepping past it fetches one more chunk of days. The arrow at the end of the range is greyed out, and since the 2026-10 theme pass both arrows are visible controls: 36x24 header-coloured panels with a thin `UI_COL_BORDER` outline (they used to be bg-on-bg). While any fetch is in flight (page step, refresh button, cold-league fill) a small comet in the active league's colour spins in the pager strip between the page counter and `>` — the board may still show the previous page for a moment, but the spinner is the answer to "is it hung or loading?". It hides the instant the cycle renders, and a hidden LVGL object costs nothing.

League tabs (`NBA | WNBA | NCAA | NCAAW` — the men's game as "NCAA", the women's as "NCAAW"): the lit tab is the feed on screen, and it wears its league's accent — NBA crimson, WNBA burnt orange, NCAA men's deep cyan, women's magenta (`league_accent()` in `cards_screen.c`; the busy comet matches). Each tab is sized from `lv_text_get_size()` for its own label + `UI_TAB_PADX` (a uniform box for "NCAAW" would overflow the header). Tapping one swaps the whole board — the tap lights the label immediately and only *queues* the switch; the feed worker applies it at a cycle boundary (cache and pager reset, header re-anchors on the new league's "today") and aborts whatever fetch was in flight at its next request, so the board never mixes two leagues and a tap lands in seconds even if a refresh was mid-flight. A tab that has been shown once this session then **paints instantly** from its RAM *page box* (four boxes, up to 18 games — six pages — each, refreshed on every successful cycle and stashed again whenever you leave the tab; the restore also lands on the page you left that tab on); the real refill follows quietly in the background. Only a cold tab (first visit since boot, before the boot pre-warm reaches it) waits on its league's source — and that source is ESPN for all four leagues now.

Header icon buttons (refresh, gear): 30 px squares (`UI_HDR_BTN_W`) with a ghost `UI_COL_BORDER` outline, zero internal padding, spacing on margins — pass-3 geometry keeps the 320 px row honest with a ~12 px buffer between the Wi-Fi badge and the controls.

Card anatomy: the main row `[logo] TEAM_A  102 - 98  TEAM_B [logo]` is **left-aligned and top-aligned** (logos ~22 px, abbrevs 16pt, score 20pt) at the same y as the game date in 12pt at the top-right — that is what lets the card be 46 px instead of 56. The centre text is capped at 100 px (`UI_CARD_CENTER_W`; strings over 12 chars step to 16pt and get `…`), so even the widest card clears the date. Along the bottom: full team names in 12pt (away left, home right) with the live game clock (`Q2 05:31`, `Halftime`) green in the middle; names clip with `…`. Away team first (broadcast convention), and the home abbreviation carries a `*` — its arena is where the game is played (no venue text fits a 46 px card; the full name is on the detail view). Winner's abbreviation and name are white, loser's muted; live scores are green. Logos come from baked flash tables (30 NBA + 13 WNBA + 182 college conference schools; see Generated tables below); a team without a baked bitmap draws a placeholder ring instead of skipping its logo. Scheduled college games show ESPN's tip text (`11/10 - 6:30 PM EST`) in the centre slot.

Tap a card → game detail view: header `AWAY @ HOME` + status (`Final`, `Q2 05:31`), the score big with both logos flanking it, a box-score table (`Q1 Q2 Q3 Q4 [OT] T` with full team names — the NBA plays quarters, MBB plays halves so its table reads `H1 H2 [OT] T`, WBB quarters), the game date + tip-off, the venue, and while a game runs the timeouts left (NBA only — ESPN's scoreboard doesn't carry them). The venue line reads the arena straight from the ESPN scoreboard — every league's scoreboard names its event's venue (exact even at neutral sites) — and falls back to the baked home-arena table when a stored game predates it; it hides when neither knows. ESPN carries per-period stats for every league, so the box-score table renders everywhere (it still hides for any payload without them). Needs no extra API call — the scoreboard response already carries the period scores.

## Screens & navigation

```
calibration (first boot only) → splash (auto, 5s) → cards
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

Every screen has a `*_screen_create(void)` factory; they are all created once in `app_main.c` as static globals (`splash_scr`, `cards_scr`, `settings_scr`, `wifi_scr`). Navigation is setter + static C callback:

```c
// In app_main.c, after creating screens:
cards_screen_set_settings_scr(settings_scr);
// In a screen .c:
static void nav_to_cards(lv_event_t *e) { lv_scr_load((lv_obj_t *)lv_event_get_user_data(e)); }
lv_obj_add_event_cb(btn, nav_to_cards, LV_EVENT_CLICKED, (void *)cards_scr);
```

Setters in use: `settings_screen_set_wifi_scr` / `_cards_scr` / `_calibration_scr`, `wifi_screen_set_settings_scr`, `cards_screen_set_settings_scr`, `game_detail_screen_set_back_scr` (the detail screen is created on the first card tap, so this is all the wiring it needs), `calibration_screen_set_done_cb` / `_back_scr`. C only — all callbacks are `static void fn(lv_event_t *e)`.

`LV_EVENT_SCREEN_LOADED` is sent to the screen object, and the cards screen uses it to request a **repaint** from the feed worker whenever it becomes visible (see the feed section). Built-in fonts are ASCII + `LV_SYMBOL_*` only; the icons in use are `LV_SYMBOL_SETTINGS`, `LV_SYMBOL_WIFI`, `LV_SYMBOL_LEFT`, `LV_SYMBOL_CLOSE`.

Boot flow: on first boot (no valid NVS calibration) → calibration screen; otherwise splash (5 s, with a live stage line: "Connecting to Wi-Fi..." → "Showing stored scores..." / "Fetching first scores...") → cards. The board repaints from cache the moment it appears, so the NVS board is up while the boot pre-warm continues in the background.

## Score feed design (`nba_service.c`)

A resident FreeRTOS worker owns **all** feed state (cache, boxes, cursor, league) — UI callbacks only ever set flags + wake it (the LVGL port mutex is not recursive, and a cross-task data mutation once let an in-flight cycle mix two leagues on one board). The pieces:

- **Fetch model** — ESPN's scoreboard per league per day (`espn_api.c`, streaming constant-memory parser, no JSON tree). One request per calendar day, 4 s pacing, one keep-alive TLS handshake per burst (`esp_http_client_set_url` reuses it across a day-walk/hunt/pre-warm chain), released before every sleep so an idle session parks no ~20 KB across the refresh gap.
- **Cycle** — every 15 min (`CYD_NBA_REFRESH_S` override): on page 1 re-anchor on today (picks up starting games and new slates); deeper pages refetch only via the header button or when they hold unfinished games. A warm-up gate holds every cycle until Wi-Fi connects (cycle 1 must never race the radio); a queued tab tap or page/refresh poke breaks the wait so offline boards stay responsive.
- **RAM window** — newest-first cache of 6 pages; the day-walk resumes below the **oldest game in the window** (never a stale fetched-day pointer) and lands with a **cap** so a walk aimed at page P slides at most one page past it (`svc_reach_page` threads `max_base` down to `svc_cache_insert` — an uncapped walk on a busy day slid past its own target, re-anchored, and wiped what it had just fetched). Empty days are remembered in a 64-day **known-empty bitmap** so a spaced calendar (WNBA Finals) walks back without re-probing a day, and `SVC_EMPTY_CHUNKS` (5) *distinct* empty days earn `s_older_exhausted`. History floor: 35 days.
- **Per-league page boxes** — each tab's newest games (up to 18) live in `s_page_box[4]`; every successful cycle refreshes the shown league's box, switching away stashes once more, and the boot **pre-warm** (first *connected* cycle, once per boot, leagues with a box inside 6 h skipped) fills the three nobody has visited. `svc_cache_load` tries the box first, then the NVS blob, and a restore snaps the pager to the page the viewer left that tab on (`s_last_page[lg]`).
- **Dead leagues** — a clean-empty newest edge triggers a forward hunt for the nearest upcoming slate (`svc_find_nearest_slate`: week-jump + bracket walk-back to +28 days, ~9 requests, remembered per league), so an off-season tab shows scheduled games + tip text instead of "No data". A clean-empty hunt is a weekly-retry verdict (`s_slate_none_at`); a probe that *errored* is never treated as empty.
- **NVS snapshot** — the anchored newest board: pro leagues 4 pages (~4.4 KB), college 1 page (~1.1 KB) into `games_{nba,wnba,ncaab,ncaaw}`. Validated header (`SVC_BLOB_MAGIC` + `sizeof(nba_game_t)`), variable length checked against `count`, per-game `league` field checked — a clipped write, a foreign depth, or cross-league pollution self-rejects. While the pager is deep (`s_base > 0`) the write is refused: fetched history must never become the boot snapshot or a tab's instant paint.
- **Render discipline** — `svc_render()` skips pushing a page into a **hidden** screen (the detail view needs the LVGL pool), so the cards screen requests a paint on every `SCREEN_LOADED` via `nba_service_repaint()` (flag + wake, *not* `s_dirty`): consumed at the offline gate, cycle top, each pre-warm league, each prefetch day, and the idle/backoff sleeps, where a repaint wake must not count as a refresh poke. Empty cache paints "Loading...".
- **Idle prefetch** — after a successful, untouched cycle, up to 3 more days walk into the cache's free slots (capped so a prefetch never slides an anchored page 0) so the next `<` renders from RAM; dies the moment a tap or switch lands.
- **Backoff & floors** — failures back off 15 s → 5 min; a page step onto the exhausted edge or a walk out of budget is an *answer*, not a failure (no backoff). Reboot below 25 KB free heap; the storm-specific floors (46/48 KB) are gated on `mbedtls_arena_active()` because the arena's .bss permanently lowers the bare pool.

## Memory budget

LVGL widgets live in LVGL's own pool (`CONFIG_LV_MEM_SIZE=49152` — the key 9.6 renamed from `LV_MEM_SIZE_KILOBYTES`), measured on device — **79%** with a full 3-card page (**75%** on 9.5; the 9.6 widgets cost ~450 B more per page — re-check the line after any render change). Each card ≈ 3.2 KB: 7 labels + 2 name labels + 2 logo widgets; the logo pixels themselves stay in flash; the header tabs cost ~1 point, the pager busy-spinner ~750 B. The pool at the arena shape booted around 37.7 K before the 6-page boxes (31.1 K now); the stock 46 K storm floor and 48 K pre-warm gate are gated on `mbedtls_arena_active()` (both would read as permanent failures at this shape), and the 25 KB absolute floor stays load-bearing everywhere — the ~200 B/connection lwIP drift lives there and still self-heals via roughly one reboot per week. TLS record buffers are still the per-connection allocation: `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` must stay 16384 — 8192 passes handshakes but breaks the body read against Akamai's 16 KB records, 4096 breaks the handshake (documented in `sdkconfig.defaults`).

A page of cards (~10 KB) and the detail view (~13 KB) do not sit side by side: tapping a card drops the page, the feed skips re-rendering into a hidden screen, the page is rebuilt from the cache when the board comes back, and the detail view frees itself on the way back (device-measured: the detail view peaks at **86%** of the 48 K pool and the board returns to its 79% baseline on the way out).

Fetched games sit in static arrays outside that pool — the 6-page cache (18 games), four 18-game page boxes and one 24-game parse page; the NVS blob staging is **aliased over the parse page** (both are worker-only and never live at the same moment — that 4.4 KB clawback is what paid for the boxes growing from 4 to 6 pages, boot heap 37.7 K → 31.1 K, still ~6 K clear of the 25 K floor). `sizeof(nba_game_t)` = 368 B: page depth is a `.bss` budget decision — price a depth knob in bytes-per-slot × all-arrays before touching it, then look for non-overlapping lifetimes to union.

The 3-card shape is what pays for the **44 KB mbedTLS static arena** (`.bss`, ON by default): handshakes draw from the arena instead of the system allocator, so the cert-storm failure band (~45.5 K free heap with the stock allocator) is gone — device-proven 100/100 alternating ESPN/Google handshakes green at 14,360 B free heap (`tools/mbedtls_opt/README.md`).

The logo bitmaps (~315 KB packed — the 182-school NCAA table is ~256 KB of it) are all linked into the binary: **~1.77 MB of the 2 MB app partition, ~16% free**. Flash is the scarce resource now — measure before baking any more art.

## API budget

All four leagues ride ESPN's public scoreboard (no key, no published limit — be polite anyway): one request per calendar day, `espn_api` paces itself at 4 s between calls, requests inside one burst share **one keep-alive TLS connection** (the handshake is the expensive part — the worker releases the connection when it goes idle, so a day-walk/pre-warm burst costs one handshake and the idle pool holds nothing), and failures back off 15 s → 5 min instead of retrying in a loop. Each day keeps only its **24 newest** games (a November college slate can list 60+; ESPN orders them oldest-tip-first, so keeping the first N would drop the evening games). Paging inside the 6 cached pages costs nothing at all; stepping past the older edge fetches one more day, and the pre-warm (fired on the first *connected* cycle, once per boot) spends ~10 background requests (plus the once-a-day slate hunt, and a weekly re-probe only for a league whose hunt came up clean-empty) to make every tab instant; a tab whose box is still recent is skipped. The clock sync is one unpaced HEAD probe per boot, run inside the first *connected* cycle (the worker's warm-up gate keeps it off a dead radio). The newest games of every league are snapshotted to NVS (`games_nba`/`games_wnba` at 12 games ~4.4 KB, `games_ncaab`/`games_ncaaw` at one page ~1.1 KB): a reboot shows that board in ~8 s with zero API calls, and a reboot *inside* the refresh interval skips the first fetch entirely.

## Generated tables & assets (never hand-edit)

### Team logos (`main/team_logos.c`)

Regenerate with `python3 tools/make_team_logos.py [--league nba|wnba|ncaa|all]` (needs `pillow` + `cairosvg`). ~22 px RGB565A8 const bitmaps for 30 NBA + 13 current WNBA + **conference-trimmed** NCAA D1 schools — 182 power-5/group-of-5-core by default (membership from one ESPN standings call, cached `ncaa_conferences.json`; `--all-conferences` bakes all 362), ~315 KB of bitmaps total. NBA art from `cdn.nba.com` SVGs (ESPN PNGs as fallback), WNBA and NCAA from `a.espncdn.com/i/teamlogos/{wnba,ncaa}/500/<espn_team_id>.png`; pro team lists from balldontlie's `/teams` endpoints (the WNBA one 404s intermittently — the script carries a baked 13-team fallback). The script writes a labelled `tools/logos/contact_sheet.png` — **eyeball it before trusting an id mapping**: balldontlie's team ids are not linearly related to the NBA.com CDN ids, and ESPN school ids are not guessable either (UK=96, MSU=127, NKU=94). Downloads cache under `tools/logos/`.

balldontlie NBA and WNBA ids are **separate overlapping namespaces**, and ESPN school ids are a third, so there is one accessor per league (`team_logo` / `wnba_team_logo` / `ncaa_team_logo`) behind the `game_logo(league, id)` dispatcher in `league_logos.c/h` — pick by the feed the game came from, never by the number. NCAAB and NCAAW share the single NCAA school table. Teams outside the baked set draw cards_screen's placeholder ring, so trimming costs no layout — just art. Footprint: NBA ~44 KB + WNBA ~18 KB + NCAA ~256 KB ≈ 315 KB (~15% of the partition); ~600 B of LVGL pool per card; zero HTTP.

### Team venues (`main/team_venues.c`)

Fallback home arenas for the 30 NBA + 13 WNBA teams, keyed per league by balldontlie id. `python3 tools/make_team_venues.py [--refresh]` walks ESPN's pro **scoreboards** day-by-day across season windows (the `/teams` endpoints carry no venue) and keeps each home team's arena, cached at `tools/logos/venues_harvest.json`. Join key is the team **full name** — ESPN's pro abbreviations differ from balldontlie's, and balldontlie's roster lists retired franchises whose names collide with live ones (id 50 = the BAA Denver Nuggets) — so the table is capped to the current namespace and warns on holes inside it. College needs no table: the ESPN scoreboard carries `competitions[].venue` per game (`→ nba_game_t.venue`, exact at neutral sites, `", City ST"` appended only when the whole tail fits 48 chars). Cards carry a `*` on the home abbreviation; the full arena name is one tap away on the detail view.

### Pro id map (`main/pro_team_ids.c`)

The ESPN scoreboard's team ids are **not** the balldontlie ids every consumer speaks (logos, venues, NVS blobs), and the namespaces overlap (ESPN 17 = BKN, balldontlie 17 = POR), so the parse boundary translates through this generated table: `python3 tools/make_pro_id_map.py` (join = team full name, capped to NBA 30 / WNBA 13; unmapped → id 0 → the placeholder logo ring, never a raw ESPN id). Every downstream consumer kept working when the feed migrated from balldontlie to ESPN — old blobs stayed valid without a magic bump.

### Splash image

Replace `tools/basketball.png`, then `cd tools && ./convert_image.sh` (needs the `lv-img-conv` Python package); outputs the LVGL C array to `main/`.

## Host UI preview (`tools/sim/`)

`tools/sim/run.sh` compiles the real screen `.c` files against host LVGL (`lv_conf.h` in `tools/sim/`, fonts matched to `sdkconfig`), renders into an RGB565 framebuffer and dumps PNGs to `/tmp/opencode/cards_<scenario>.png` — layout changes need no hardware. Mock game records (final / live / scheduled / postponed, NBA + a college slate) live in `tools/sim/main.c`; `SIM_HEADER_DUMP=1 ./build/cyd_sim ncaa` prints every header child's x/w so the fit is measured, not squinted at.

`run.sh` also runs the **ESPN parser test** over real captured API bodies (`tools/sim/fixtures/espn_*.json` — one busy final day per league plus a 69-event November scheduled day): the exact streaming scanner the device uses, with assertions on halves-vs-quarters, OT linescores, the scheduled "0-0" guard, the newest-24 cap, the pro id remap, and the US-game-day date rule (checked per game against glibc `timegm`). **If you touch `espn_api.c`, that test must stay green** — it caught three device-visible bugs before they were ever flashed.

To preview a new screen: add its `.c` to `add_executable` in `tools/sim/CMakeLists.txt`, call its `*_screen_create()` from `tools/sim/main.c`, and stub whatever ESP-IDF symbols it links against (see `tools/sim/shim/` and `tools/sim/stubs.c`).
