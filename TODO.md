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

## In Progress (Blocking)

*(None — touch integration is complete and wired)*

## Next

### 1. Wi-Fi connectivity (esp_wifi backend)
- Connect `wifi_screen` Wi-Fi switch to `esp_wifi` start/stop
- Scan & display real networks in network list
- Select network → show password input → connect
- Display connected IP in IP config section

### 2. NBA API integration
- Fetch live scores from NBA API (or alternative source)
- Populate cards container with real game data
- Auto-refresh on interval

### 3. Live score cards
- Team logos, scores, game status display
- Tap card → game detail view

### 4. Game detail view
- Tap a card to see expanded game info

### 5. NVS brightness persistence
- Brightness slider already saves to NVS on change — verify it loads on boot

### 6. Touch rotation tuning
- XPT2046 driver flags (`swap_xy`, `mirror_x`, `mirror_y`) may need per-device tuning
- Current: driver flags all false, rotation handled in `touch_process_coordinates`

(End of file - total 55 lines)
