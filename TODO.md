# TODO

## Done

- [x] ESP-IDF project scaffold (CMake, sdkconfig, deps)
- [x] LVGL 9 integration with ST7789 SPI display on CYD2USB
- [x] Splash screen: basketball icon + "Sami's basketball" / "score viewer"
- [x] Cards screen: "NBA SCORES" header + empty scroll container
- [x] 3-second splash then transition to cards screen
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

## In Progress (Blocking)

### 1. Fix screen creation wiring (in `app_main.c`)
- Only `touch_test_scr` is created; splash, cards, settings, wifi screens are **never created**
- Need to create all screens and wire navigation:
  - `cards_scr = cards_screen_create()` → `cards_screen_set_settings_scr(settings_scr)`
  - `settings_scr = settings_screen_create()` → setters for wifi_scr, cards_scr
  - `wifi_scr = wifi_screen_create()` → setter for settings_scr
- Load `splash_scr` first, then 3s timer → `cards_scr`
- Remove `touch_test_scr` or make it accessible via settings → touch test nav

### 2. Fix `coords_label` reference (in `app_main.c:215` / `touch_test_screen.c`)
- `touch_integration_set_coords_label(coords_label)` references `coords_label` directly
- `coords_label` is **static** inside `touch_test_screen.c` but declared `extern` in `touch_test_screen.h`
- Fix: Expose via function in `touch_test_screen.h/.c` (e.g. `touch_test_screen_get_coords_label()`)
- Or: `touch_integration` should store label internally instead of relying on external reference

### 3. Tune touch coordinate mapping (in `touch_integration.c`)
- User reported X scale "still not reaching 320" — coordinate transforms need testing/tuning
- Current flow: XPT2046 config (`swap_xy=false, mirror_x=true, mirror_y=false`) → `touch_process_coordinates` callback swaps X/Y and mirrors both → LVGL `touch_read_cb` clamps to `LV_HOR_RES/LV_VER_RES`
- Test on device, adjust as needed
- Key question: Does `process_coordinates` callback fire **before** or **after** the built-in mirror transforms? May need to remove redundant mirroring

### 4. Verify touch input end-to-end
- Once screens are wired and coords are fixed, test tap on all buttons
- Verify: gear button → settings, back button → cards, wifi button → wifi screen
- Remove/replace `touch_test_scr` with proper app flow

## Next (Independent of Touch Fixes)

### 5. Save brightness to NVS
- Persist brightness setting across reboots
- Load NVS value on boot, save on slider change

### 6. Wi-Fi connectivity (esp_wifi backend)
- Connect `wifi_screen` Wi-Fi switch to `esp_wifi` start/stop
- Scan & display real networks in network list
- Select network → show password input → connect
- Display connected IP in IP config section

### 7. NBA API integration
- Fetch live scores from NBA API (or alternative source)
- Populate cards container with real game data
- Auto-refresh on interval

### 8. Live score cards
- Team logos, scores, game status display
- Tap card → game detail view

### 9. Game detail view
- Tap a card to see expanded game info

(End of file - total 55 lines)
