# UI preview harness (host LVGL, no hardware)

Renders the real screen code from `cyd_basketball_scores/main/` on the PC and
writes 320x240 PNGs, so layout work can be checked without flashing.

```sh
./run.sh            # -> /tmp/opencode/cards_{games,live,empty,offline,paged,oldest,detail,detail_live,detail_ot,detail_sched}.png
./build/cyd_sim games /tmp/opencode/out.ppm   # single scenario
SIM_TREE=1 ./build/cyd_sim detail /tmp/out.ppm  # dump widget geometry (x/width/hidden)
```

`paged` / `oldest` render the page pager mid-history (a page straddling two dates) and
at the older edge (history exhausted, so the `<` arrow is greyed out).

`detail*` render the game detail screen (final / live / overtime / scheduled). They keep
the board at its placeholder page, because on the device the card page is dropped when a
game opens — that is the LVGL pool situation that matters. `lv_conf.h` sets
`LV_MEM_SIZE` to the device's 64 KB, so the `lv pool:` line each scenario prints is
directly comparable with the device log.

The mock slate carries real balldontlie `team_id`s and `team_logos.c` is compiled in, so
the previews show the same baked logos the device draws (regenerate it with
`tools/make_team_logos.py`; the sources/cache are in gitignored `tools/logos/`).

## How it works

| File | Role |
|------|------|
| `CMakeLists.txt` | builds LVGL (from `managed_components/lvgl__lvgl`) + the screen `.c` files under test |
| `lv_conf.h` | host config; fonts/colour depth matched to the device `sdkconfig` (`CONFIG_LV_FONT_MONTSERRAT_*`) |
| `shim/` | stand-ins for `esp_err.h`, `esp_log.h`, `esp_system.h`, `esp_wifi_types.h` |
| `stubs.c` | no-op implementations of device-only modules the screens call (`nba_service`) |
| `main.c` | 320x240 RGB565 framebuffer + flush-to-nothing, mock data, PPM dump |

`run.sh` converts the PPM to PNG with ImageMagick; open the PNG to inspect pixels.

## Adding a screen

1. Add `main/<screen>.c` to `add_executable` in `CMakeLists.txt`.
2. Call its `*_screen_create()` from `main.c` (and any setters it needs, e.g.
   `settings_screen_set_wifi_scr(dummy)` — pass `lv_obj_create(NULL)` dummies).
3. If it links against a device module (wifi_backend, nvs_settings, ...), add a
   stub in `stubs.c` rather than compiling the real one.
4. Add a scenario branch in `select_scenario()` and a name to the loop in `run.sh`.

## Gotchas (already paid for)

- `lv_conf_template.h` wraps its body in `#if 0`; the copy here must be `#if 1`,
  otherwise every font/colour define is ignored (LVGL prints a `#pragma message`
  hint about a missing `lv_conf.h`).
- `LV_HOR_RES` / `LV_VER_RES` are runtime macros that read the *default display*;
  before `lv_display_create()` they are 0, which makes the refresh loop spin
  forever. Use `SIM_W` / `SIM_H`.
- Only what `main.c` calls is exercised: no network, NVS, touch, or the LVGL port
  lock. Real hardware still matters for those.
