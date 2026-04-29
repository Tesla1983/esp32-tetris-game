#pragma once

#include <stdint.h>

#include "esp_err.h"

#define ST7735_H_RES 128
#define ST7735_V_RES 160

esp_err_t st7735_init(void);
esp_err_t st7735_draw_frame(const uint16_t *framebuffer, int width, int height);
