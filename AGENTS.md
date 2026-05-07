# AI Agent Instructions for esp32_st7789_tetris

## Project Overview
- ESP32 firmware project using **ESP-IDF** and **LVGL**.
- Implements a minimal **Tetris** demo for an **ST7735** SPI display (128x160, RGB565).
- The app is written in **C** and optimized for ESP32 display rendering.

## Key Files and Structure
- `CMakeLists.txt` - project entry point.
- `main/main.c` - application entry, calls `tetris_start()`.
- `main/CMakeLists.txt` - registers the `main` component.
- `components/tetris/` - game logic, display driver, and hardware-specific code.
- `components/tetris/tetris_game.c` - Tetris state machine, framebuffer rendering, and FPS monitoring.
- `components/tetris/drivers/st7735.c` - ST7735 SPI display driver.
- `components/tetris/tetris.h` - public start API.
- `main/idf_component.yml` - IDF component manifest, depends on `lvgl/lvgl`.

## Build and Flash
- Use ESP-IDF from the activated environment.
- Common commands:
  - `idf.py set-target esp32`
  - `idf.py build`
  - `idf.py -p <PORT> flash monitor`
- The repo is configured for the ESP32 target; do not change the target unless the hardware or chip changes.

## Development Notes
- The project uses a **double-buffered framebuffer** and asynchronous DMA via `st7735_draw_frame_async()`.
- The LCD driver and rendering path are tightly coupled to timing and buffer swapping.
- UI/game logic is currently an **automatic demo**. Manual input handling is not implemented in the current code.
- SPI pin assignments are defined in `components/tetris/tetris_game.c` and may need adjustment for different ESP32 boards.

## What AI agents should focus on
- Preserve the existing rendering architecture when modifying display performance or drawing code.
- Keep the component separation: `main` is startup, `tetris` contains game+display.
- Do not assume there are tests; validate changes via `idf.py build` and `monitor` if possible.
- For hardware-related fixes, refer to the ST7735 initialization and RGB565 color format sections in `components/tetris/drivers/st7735.c`.

## Notes for contributors
- This repo is a self-contained demo; most logic lives in `components/tetris/tetris_game.c`.
- Use `ESP_LOGI()` messages for runtime diagnostics, especially around frame timing and display initialization.
- Keep changes minimal when adjusting board geometry or color definitions; the board is currently hardcoded to `10x13` cells.
