# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System and Common Commands

This is an ESP-IDF project for ESP32-S3 with LVGL porting. Use the following commands:

### Build and Flash
```bash
# Set target (required first time)
idf.py set-target esp32s3

# Configure project
idf.py menuconfig

# Build the project
idf.py build

# Flash and monitor
idf.py -p PORT flash monitor

# Clean build
idf.py fullclean
```

### Configuration
- Use `idf.py menuconfig` to configure project settings
- Navigate to "Example Configuration" → "Display" for LVGL/LCD settings
- Key configurations are also in `sdkconfig.defaults`

## Project Architecture

### Core Components
1. **Main Application** (`main/main.c`):
   - Entry point that initializes LCD and runs LVGL demos
   - Calls `waveshare_esp32_s3_rgb_lcd_init()` and demo functions

2. **LVGL Port** (`main/lvgl_port.c/h`):
   - Handles LVGL initialization and task management
   - Implements double/triple buffering for tear-free rendering
   - Manages mutex for thread-safe LVGL operations
   - Supports screen rotation (0°, 90°, 180°, 270°)

3. **LCD Driver** (`main/waveshare_rgb_lcd_port.c/h`):
   - Configures RGB LCD panel (ST7701 controller)
   - Manages touch controller (GT911) via I2C
   - Defines GPIO pin mappings for LCD data/control signals

### Key Features
- **Avoid Tearing**: Three modes available via `EXAMPLE_LVGL_PORT_AVOID_TEAR_MODE`
  - Mode 1: LCD double-buffer & LVGL full-refresh
  - Mode 2: LCD triple-buffer & LVGL full-refresh  
  - Mode 3: LCD double-buffer & LVGL direct-mode (recommended)
- **Touch Support**: GT911 touch controller with I2C interface
- **Multiple Demos**: Widgets, music, benchmark, and stress test demos
- **Display Rotation**: Configurable via menuconfig

### Hardware Configuration
- **Target**: ESP32-S3 with 8MB flash, PSRAM enabled
- **Display**: 800x480 RGB LCD with ST7701 controller
- **Touch**: GT911 I2C touch controller
- **Clock**: 240MHz CPU, 80MHz SPIRAM, 16MHz LCD pixel clock

### File Structure
- `main/` - Application code
- `components/` - External components (LVGL, touch drivers)
- `CMakeLists.txt` - Build configuration
- `sdkconfig.defaults` - Default configuration
- `README.md` - Hardware setup and usage instructions

### Development Tips
- Use `lvgl_port_lock()` before calling LVGL functions from other tasks
- Touch reset and GPIO initialization handled in `waveshare_esp32_s3_touch_reset()`
- RGB LCD uses DMA for efficient memory transfers
- LVGL buffer allocation configurable between PSRAM and internal RAM