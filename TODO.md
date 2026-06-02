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
- [x] Calibration screen (4 corners, raw ADC display, long-press undo, quadrant validation)
- [x] Calibration button in settings screen
- [x] Touch integration rewrite: LVGL indev callback, XPT2046 mapping, no redundant poll task

## In Progress (Blocking)

### 1. Rewrite touch integration for LVGL (in `touch_integration.c`)
- Remove redundant `touch_poll_task` — LVGL indev read callback handles polling
- Add `map()` function to convert raw XPT2046 coordinates to LCD resolution
- Simplify `touch_process_coordinates` to only do linear mapping (not double-transform)
- Update XPT2046 config flags: `swap_xy=true, mirror_x=true, mirror_y=false` (matches display)
- Remove `touch_integration_set_coords_label()` — use calibration screen instead
- Use component constants `TOUCH_X_RES_MIN/MAX` etc. for mapping (calibrate later on-device)

### 2. Create calibration screen (new `calibration_screen.c/.h`) — DONE
- Display 4 crosshair points (4 corners) for touch calibration
- Show raw XPT2046 coordinates on tap for manual calibration factor calculation
- "Done" button saves calibration and returns to previous screen
- Accessible from settings panel
- Long-press any crosshair to undo the last tap (press and hold ~1s, then release)
- Quadrant checking: each tap must land in the expected quadrant (left/right, top/bottom) or it's rejected
- Debouncing: 500ms cooldown between taps to prevent accidental double-taps

### 3. Fix screen creation wiring (in `app_main.c`)
- Create all screens: splash, cards, settings, wifi, calibration, touch_test
- Wire navigation:
  - `cards_scr = cards_screen_create()` → `cards_screen_set_settings_scr(settings_scr)`
  - `settings_scr = settings_screen_create()` → setters for wifi_scr, cards_scr, calibration_scr
  - `wifi_scr = wifi_screen_create()` → setter for settings_scr
  - `calibration_scr = calibration_screen_create()` → setter for previous screen
- Load splash first, then 3s timer → cards_scr
- On first boot (no NVS calibration), load calibration_scr before splash

### 4. Fix `coords_label` reference (in `touch_test_screen.c/h`)
- Add `touch_test_screen_get_coords_label()` getter function
- Remove direct `extern lv_obj_t *coords_label` declaration
- Remove `touch_integration_set_coords_label()` call from app_main.c

### 5. Add calibration button to settings_screen
- Add "Touch Calibration" button in settings content area
- Navigate to calibration screen

### 6. Verify touch input end-to-end
- Once screens are wired and coords are fixed, test tap on all buttons
- Verify: gear button → settings, back button → cards, wifi button → wifi screen
- Test calibration screen with all 5 points

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
