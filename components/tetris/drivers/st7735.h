#pragma once

#include <stdint.h>

#include "esp_err.h"

#define ST7735_H_RES 128
#define ST7735_V_RES 160

esp_err_t st7735_init(void);
esp_err_t st7735_draw_frame(const uint16_t *framebuffer, int width, int height);

/**
 * @brief 异步 DMA 方式发送一帧数据（非阻塞，需配合 st7735_wait_frame 使用）
 */
esp_err_t st7735_draw_frame_async(const uint16_t *framebuffer);

/**
 * @brief 等待上次异步 DMA 传输完成
 */
esp_err_t st7735_wait_frame(void);
