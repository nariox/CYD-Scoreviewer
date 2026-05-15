# CYD Basketball Scores

LVGL 9 + ESP-IDF basketball score viewer for the **CYD2USB** (ESP32-2432S028, dual USB port variant).

## Hardware

- **Board**: ESP32-2432S028 (CYD2USB, no "R")
- **Display**: 320x240 ST7789 (SPI, HSPI bus)
- **Touch**: XPT2046 resistive touch (SPI3, not yet integrated)
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

- Splash screen → cards screen (3s transition)
- Touch not yet integrated
- Wi-Fi / NBA API not yet connected
