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
│   ├── cards_screen.c/.h   # NBA scores card list
│   ├── basketball_img.c/.h # LVGL image descriptor (64x64)
│   └── ...more screens
tools/
├── basketball.png          # Source image
├── convert_image.sh        # LVGL image converter script
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

## Current State

- **Boot flow**: On first boot (no NVS calibration) → calibration screen. After calibration → splash (3s) → cards
- **Touch**: XPT2046 calibrated, LVGL indev working, linear coordinate mapping
- **Calibration**: 4-corner crosshair calibration with long-press undo, quadrant validation, debounce
- **NVS**: Calibration data + saved flag persisted across reboots, validated on load (range 0-4095)
- **Settings**: Brightness slider (saves to NVS), Wi-Fi button, Touch Calibration button
- **Wi-Fi / NBA API**: Not yet connected

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
- Touch must match: XPT2046 config uses same `swap_xy=1, mirror_x=1, mirror_y=0`
- Backlight: GPIO 21, currently `gpio_set_level()`, plan to migrate to LEDC PWM for brightness control

### Available Symbols (baked into Montserrat 16)
- `LV_SYMBOL_SETTINGS` — gear icon (for settings button)
- `LV_SYMBOL_WIFI` — Wi-Fi icon
- `LV_SYMBOL_LEFT` — left arrow (back button)
- `LV_SYMBOL_CLOSE` — close icon

### Current Screen Architecture
```
calibration (first boot only) → splash (auto, 3s) → cards
                                                                    ├── [gear button] → settings
                                                                    │                    ├── [back] → cards
                                                                    │                    ├── [wifi btn] → wifi
                                                                    │                    │                    ├── [back] → settings
                                                                    │                    │                    └── [switch] → placeholder
                                                                    │                    └── [calibration btn] → calibration
                                                                    │                                                         ├── [back] → settings
                                                                    │                                                         └── [done & save] → splash → cards
                                                                    └── (future: tap card → game detail)
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
- `calibration_screen_set_done_cb(callback)` — sets callback for calibration "Done & Save" button
- `calibration_screen_set_back_scr(scr)` — sets back navigation target (settings screen)

### Known Issues
- `sdkconfig` may need `IDF_TARGET=esp32` (run `idf.py set-target esp32`)

### Lessons Learned
- **LVGL timer**: `lv_timer_create()` creates an infinite-repeat timer by default (`repeat_cnt=0`). Use `lv_timer_set_repeat_count(timer, 1)` for one-shot timers. The `lv_timer_t` struct is opaque — don't access members directly.
- **NVS validation**: Always validate calibration data ranges (0-4095 for XPT2046) before trusting the `cal_saved` flag. Corrupt NVS data can cause inverted touch mapping, making buttons auto-fire on wrong screens.
- **Flash and NVS**: Using `esptool merge-bin` may not erase the NVS partition. If calibration data is corrupt, run `idf.py erase-flash` before flashing, or rely on the built-in validation that resets corrupt data to defaults.
