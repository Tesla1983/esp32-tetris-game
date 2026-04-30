#pragma once

#include <stdint.h>

#include "esp_err.h"

#define ST7735_H_RES 128
#define ST7735_V_RES 160

esp_err_t st7735_init(void);
esp_err_t st7735_draw_frame(const uint16_t *framebuffer, int width, int height);
esp_err_t st7735_draw_region_dma(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                 const uint16_t *pixels);
