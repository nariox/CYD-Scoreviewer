# CYD Basketball Scores

LVGL 9 + ESP-IDF basketball score viewer for the **CYD2USB** (ESP32-2432S028, dual USB port variant).

Four leagues — NBA, WNBA, NCAA men, NCAA women — from ESPN's public scoreboard (no key), kept in a resident feed worker with a rolling RAM window and an NVS snapshot, so every tab paints in seconds: a tab you have visited this session comes from its RAM page box (~200 ms), and after a reboot the last-fetched board appears ~8 s in with zero API calls — even with no network reachable at all.

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

## What it does

- **League tabs** (`NBA | WNBA | NCAA | NCAAW`) — the lit tab wears its league's accent; a tap lights the label and *queues* the switch, which the feed worker applies at a cycle boundary (a board never mixes two leagues)
- **Paging through days** — a page is exactly 3 cards (it never scrolls); `<` walks ESPN's scoreboards one day per request, six pages stay cached, and idle prefetch loads the next `<` from RAM. A comet spinner in the league's colour answers "hung or loading?"
- **Score cards** — `[logo] TEAM_A  102 - 98  TEAM_B* [logo]`, date top-right, live clock or tip text along the bottom (the `*` marks the home team); tap a card for the detail view: box score by period (quarters, or halves for MBB), venue, tip-off, and timeouts left while an NBA game runs
- **Boot** — splash with a live stage line, then the board; the cards screen repaints the moment it becomes visible, so the NVS board shows while the boot pre-warm keeps running in the background
- **System screens** — Wi-Fi scan/connect/save with a status badge, brightness slider, 4-point touch calibration with long-press undo; all persisted to NVS
- **Self-healing** — fetch backoff (15 s → 5 min), heap floors, a restart through the TLS failure band, and roughly one self-healing reboot per week against the stack's ~200 B/connection drift

## Build & flash

ESP-IDF (tested with v5.x / v6.x, currently 6.0.2); the LVGL 9.6 managed component resolves automatically.

```bash
source /opt/esp-idf/export.sh
cd cyd_basketball_scores
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Soak and diagnostic hooks (fast refresh, auto-paging, auto tab-switch, TLS stress, forced league / day-shift, arena sizing) are all listed in **AGENTS.md**. Layout changes can be checked without hardware: `tools/sim/run.sh` renders the real screens to PNG — see Architecture.md.

## Project Structure

```
cyd_basketball_scores/
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── app_main.c          # Entry point, display init, LVGL port, screen wiring
│   ├── splash_screen.c/.h  # Splash with stage line + 5s transition
│   ├── cards_screen.c/.h   # Score cards + league tabs + Wi-Fi badge + pager
│   ├── game_detail_screen.c/.h # tap a card: box score by period + venue
│   ├── nba_service.c/h     # Score-feed worker: day-walk, page boxes, NVS, repaint
│   ├── espn_api.c/h        # ESPN scoreboard client: constant-memory streaming parser
│   ├── time_sync.c/h       # System clock from the HTTPS Date header (no SNTP)
│   ├── team_logos.c/.h     # GENERATED logo bitmaps (NBA+WNBA+NCAA, flash)
│   ├── team_venues.c/.h    # GENERATED home arenas (NBA+WNBA)
│   ├── pro_team_ids.c/.h   # GENERATED ESPN→balldontlie id remap
│   ├── league_logos.c/.h   # game_logo(league, id) dispatcher
│   ├── nba_api.h           # shared types ONLY (nba_game_t / league_t)
│   ├── nvs_settings.c/h    # calibration, brightness, sized-blob helpers
│   ├── lcd.c/h             # SPI init + backlight PWM
│   ├── touch_integration.c # XPT2046 + LVGL indev + calibration map
│   ├── ui_common.h         # shared dimensions + colour palette
│   └── ... (wifi_screen, settings_screen, calibration_screen, splash bits)
tools/
├── make_team_logos.py      # regenerates main/team_logos.c (fetch + rasterise + pack)
├── make_team_venues.py     # regenerates main/team_venues.c (harvest ESPN scoreboards)
├── make_pro_id_map.py      # regenerates main/pro_team_ids.c (ESPN→balldontlie ids)
├── convert_image.sh        # LVGL image converter (splash basketball)
├── logos/                  # generated cache (sources, contact sheet) — gitignored
├── sim/                    # Host LVGL preview harness (renders screens to PNG)
└── ...
repo root
├── Architecture.md         # layout anatomy, feed design, memory + API budgets, tooling
├── Notes.md                # data caveats, TLS/memory history, calibration tutorial, parking lot
├── AGENTS.md               # AI-agent brief: commands + hooks, constraints, lessons learned
└── README.md               # you are here
```

## Documentation map

- **[Architecture.md](Architecture.md)** — screen/layout anatomy, the feed's design (day-walk, page boxes, pre-warm, slate hunt, NVS blobs, repaint), memory and API budgets, the generated logo/venue/id tables, the host UI preview
- **[Notes.md](Notes.md)** — ESPN data caveats (college coverage, preseason gaps, US game-day rule), the TLS/memory war story in brief, flash headroom, the calibration tutorial, parked ideas
- **[AGENTS.md](AGENTS.md)** — the AI-agent brief: every build/soak command and test hook, hard constraints, and the full lessons-learned log
