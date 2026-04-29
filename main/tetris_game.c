#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ========== ST7735 + 屏幕配置 ==========
#define LCD_HOST SPI2_HOST
#define PIN_NUM_MOSI 2
#define PIN_NUM_CLK 15
#define PIN_NUM_CS 17
#define PIN_NUM_DC 16
#define PIN_NUM_RST 4
#define PIN_NUM_BCKL 5

#define LCD_H_RES 128
#define LCD_V_RES 160

// ST7735 常用命令
#define ST7735_CMD_SWRESET 0x01
#define ST7735_CMD_SLPOUT 0x11
#define ST7735_CMD_FRMCTR1 0xB1
#define ST7735_CMD_FRMCTR2 0xB2
#define ST7735_CMD_FRMCTR3 0xB3
#define ST7735_CMD_INVCTR 0xB4
#define ST7735_CMD_PWCTR1 0xC0
#define ST7735_CMD_PWCTR2 0xC1
#define ST7735_CMD_PWCTR3 0xC2
#define ST7735_CMD_PWCTR4 0xC3
#define ST7735_CMD_PWCTR5 0xC4
#define ST7735_CMD_VMCTR1 0xC5
#define ST7735_CMD_INVOFF 0x20
#define ST7735_CMD_INVON 0x21
#define ST7735_CMD_MADCTL 0x36
#define ST7735_CMD_COLMOD 0x3A
#define ST7735_CMD_CASET 0x2A
#define ST7735_CMD_RASET 0x2B
#define ST7735_CMD_GMCTRP1 0xE0
#define ST7735_CMD_GMCTRN1 0xE1
#define ST7735_CMD_NORON 0x13
#define ST7735_CMD_DISPON 0x29
#define ST7735_CMD_RAMWR 0x2C

typedef struct {
    spi_device_handle_t spi;
} st7735_dev_t;

static const char *TAG = "tetris";
static st7735_dev_t g_lcd;

// ========== 底层 SPI 驱动（必须放前面） ==========
static inline esp_err_t st7735_send_cmd(uint8_t cmd) {
    gpio_set_level(PIN_NUM_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_transmit(g_lcd.spi, &t);
}

static inline esp_err_t st7735_send_data(const void *data, int len) {
    if (len <= 0) return ESP_OK;
    gpio_set_level(PIN_NUM_DC, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_transmit(g_lcd.spi, &t);
}

static void st7735_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    st7735_send_cmd(ST7735_CMD_CASET);
    uint8_t data[4];
    data[0] = (x0 >> 8) & 0xFF;
    data[1] = x0 & 0xFF;
    data[2] = (x1 >> 8) & 0xFF;
    data[3] = x1 & 0xFF;
    st7735_send_data(data, 4);

    st7735_send_cmd(ST7735_CMD_RASET);
    data[0] = (y0 >> 8) & 0xFF;
    data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF;
    data[3] = y1 & 0xFF;
    st7735_send_data(data, 4);

    st7735_send_cmd(ST7735_CMD_RAMWR);
}

// ========== 帧缓冲（消除闪烁） ==========
static uint16_t framebuffer[LCD_V_RES][LCD_H_RES];  // 大端 RGB565

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color_be) {
    if (x >= LCD_H_RES || y >= LCD_V_RES) return;
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            framebuffer[y + i][x + j] = color_be;
        }
    }
}

static inline void fb_draw_pixel(int x, int y, uint16_t color_be) {
    if (x >= 0 && x < LCD_H_RES && y >= 0 && y < LCD_V_RES) {
        framebuffer[y][x] = color_be;
    }
}

static inline void fb_clear(uint16_t color_be) {
    for (int y = 0; y < LCD_V_RES; ++y) {
        for (int x = 0; x < LCD_H_RES; ++x) {
            framebuffer[y][x] = color_be;
        }
    }
}

static void fb_flush(void) {
    st7735_set_window(0, 0, LCD_H_RES - 1, LCD_V_RES - 1);
    st7735_send_data((uint8_t *)framebuffer, LCD_H_RES * LCD_V_RES * 2);
}

// ========== ST7735 初始化 ==========
static esp_err_t st7735_init(void) {
    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * 2 + 8,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 2,
    };

    ESP_ERROR_CHECK(gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT));

    ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    ret = spi_bus_add_device(LCD_HOST, &devcfg, &g_lcd.spi);
    if (ret != ESP_OK) return ret;

    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    st7735_send_cmd(ST7735_CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));
    st7735_send_cmd(ST7735_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    st7735_send_cmd(ST7735_CMD_FRMCTR1);
    st7735_send_data(frmctr1, sizeof(frmctr1));

    const uint8_t frmctr2[] = {0x01, 0x2C, 0x2D};
    st7735_send_cmd(ST7735_CMD_FRMCTR2);
    st7735_send_data(frmctr2, sizeof(frmctr2));

    const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    st7735_send_cmd(ST7735_CMD_FRMCTR3);
    st7735_send_data(frmctr3, sizeof(frmctr3));

    uint8_t invctr = 0x07;
    st7735_send_cmd(ST7735_CMD_INVCTR);
    st7735_send_data(&invctr, 1);

    const uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
    st7735_send_cmd(ST7735_CMD_PWCTR1);
    st7735_send_data(pwctr1, sizeof(pwctr1));

    uint8_t pwctr2 = 0xC5;
    st7735_send_cmd(ST7735_CMD_PWCTR2);
    st7735_send_data(&pwctr2, 1);

    const uint8_t pwctr3[] = {0x0A, 0x00};
    st7735_send_cmd(ST7735_CMD_PWCTR3);
    st7735_send_data(pwctr3, sizeof(pwctr3));

    const uint8_t pwctr4[] = {0x8A, 0x2A};
    st7735_send_cmd(ST7735_CMD_PWCTR4);
    st7735_send_data(pwctr4, sizeof(pwctr4));

    const uint8_t pwctr5[] = {0x8A, 0xEE};
    st7735_send_cmd(ST7735_CMD_PWCTR5);
    st7735_send_data(pwctr5, sizeof(pwctr5));

    uint8_t vmctr1 = 0x0E;
    st7735_send_cmd(ST7735_CMD_VMCTR1);
    st7735_send_data(&vmctr1, 1);

    st7735_send_cmd(ST7735_CMD_INVOFF);

    uint8_t madctl = 0xC0;  // 180度旋转（MX+MY），无白边
    st7735_send_cmd(ST7735_CMD_MADCTL);
    st7735_send_data(&madctl, 1);

    uint8_t colmod = 0x05;  // RGB565
    st7735_send_cmd(ST7735_CMD_COLMOD);
    st7735_send_data(&colmod, 1);

    const uint8_t gmctrp1[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                               0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    st7735_send_cmd(ST7735_CMD_GMCTRP1);
    st7735_send_data(gmctrp1, sizeof(gmctrp1));

    const uint8_t gmctrn1[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                               0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};
    st7735_send_cmd(ST7735_CMD_GMCTRN1);
    st7735_send_data(gmctrn1, sizeof(gmctrn1));

    st7735_send_cmd(ST7735_CMD_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));

    st7735_send_cmd(ST7735_CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));

    gpio_set_level(PIN_NUM_BCKL, 1);
    ESP_LOGI(TAG, "ST7735 init done (128x160)");
    return ESP_OK;
}

// ========== 俄罗斯方块逻辑 ==========
#define BOARD_W 10
#define BOARD_H 13

#define CELL_SIZE 12
#define OFFSET_X (((LCD_H_RES - BOARD_W * CELL_SIZE) / 2))
#define OFFSET_Y (((LCD_V_RES - BOARD_H * CELL_SIZE) / 2))

#define COLOR_BG 0xFFFF    // 白色
#define COLOR_GRID 0xCE79  // 灰色网格线

static const uint16_t piece_colors[7] = {
    0x07FF,  // I
    0xFFE0,  // O
    0xF81F,  // T
    0x07E0,  // S
    0xF800,  // Z
    0x001F,  // J
    0xFD20,  // L
};

static uint8_t board[BOARD_H][BOARD_W];

typedef struct {
    int type;
    int rot;
    int x;
    int y;
} piece_t;

static piece_t cur;

// 7种方块，4个旋转状态，4x4矩阵
static const uint8_t tetromino[7][4][4][4] = {
    // I
    {{{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}},
    // O
    {{{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
    // T
    {{{0, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
    // S
    {{{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}}},
    // Z
    {{{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
    // J
    {{{1, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}}},
    // L
    {{{0, 0, 1, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}},
     {{1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
};

static void draw_cell(int bx, int by, uint16_t color) {
    int x = OFFSET_X + bx * CELL_SIZE;
    int y = OFFSET_Y + by * CELL_SIZE;
    fb_fill_rect(x, y, CELL_SIZE - 1, CELL_SIZE - 1, color);
    fb_fill_rect(x + CELL_SIZE - 1, y, 1, CELL_SIZE, COLOR_GRID);
    fb_fill_rect(x, y + CELL_SIZE - 1, CELL_SIZE, 1, COLOR_GRID);
}

static bool collision(const piece_t *p, int nx, int ny, int nrot) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!tetromino[p->type][nrot][r][c]) continue;
            int x = nx + c;
            int y = ny + r;
            if (x < 0 || x >= BOARD_W || y >= BOARD_H) return true;
            if (y >= 0 && board[y][x]) return true;
        }
    }
    return false;
}

static void merge_current_piece(void) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (tetromino[cur.type][cur.rot][r][c]) {
                int x = cur.x + c;
                int y = cur.y + r;
                if (x >= 0 && x < BOARD_W && y >= 0 && y < BOARD_H) {
                    board[y][x] = (uint8_t)(cur.type + 1);
                }
            }
        }
    }
}

static void clear_lines(void) {
    for (int y = BOARD_H - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < BOARD_W; ++x) {
            if (!board[y][x]) {
                full = false;
                break;
            }
        }
        if (full) {
            for (int yy = y; yy > 0; --yy) memcpy(board[yy], board[yy - 1], BOARD_W);
            memset(board[0], 0, BOARD_W);
            y++;  // 重新检查当前行
        }
    }
}

static void spawn_piece(void) {
    static uint32_t seed = 0x12345678;
    seed = seed * 1664525u + 1013904223u;
    cur.type = seed % 7;
    cur.rot = 0;
    cur.x = 3;
    cur.y = -1;
    if (collision(&cur, cur.x, cur.y, cur.rot)) {
        memset(board, 0, sizeof(board));
    }
}

static void render(void) {
    fb_clear(COLOR_BG);
    for (int y = 0; y < BOARD_H; ++y) {
        for (int x = 0; x < BOARD_W; ++x) {
            if (board[y][x]) {
                draw_cell(x, y, piece_colors[board[y][x] - 1]);
            }
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (tetromino[cur.type][cur.rot][r][c]) {
                int x = cur.x + c;
                int y = cur.y + r;
                if (x >= 0 && x < BOARD_W && y >= 0 && y < BOARD_H) {
                    draw_cell(x, y, piece_colors[cur.type]);
                }
            }
        }
    }
    fb_flush();
}

static int fall_speed = 6;

static void game_task(void *arg) {
    (void)arg;
    memset(board, 0, sizeof(board));
    spawn_piece();
    render();

    int tick = 0;
    while (1) {
        if ((tick % 30) == 0) {  // 旋转和移动独立于下落速度，每个 tick
                                 // 都检查一次旋转和左右移动的输入（这里用自动模拟，实际应用中应替换为按键输入）
            int test_r = (cur.rot + 1) & 3;
            if (!collision(&cur, cur.x, cur.y, test_r)) cur.rot = test_r;
        }
        if ((tick % 10) == 0) {  // 删除或注释这段自动左右移动的代码，改为手动控制
            int dir = ((tick / 10) & 1) ? 1 : -1;
            int test_x = cur.x + dir;
            if (!collision(&cur, test_x, cur.y, cur.rot)) cur.x = test_x;
        }

        if ((tick % fall_speed) == 0) {  // 下落速度由 fall_speed 控制，每 fall_speed 个 tick 下落一次
            if (!collision(&cur, cur.x, cur.y + 1, cur.rot)) {
                cur.y++;
            } else {
                merge_current_piece();
                clear_lines();
                spawn_piece();
            }
            render();  // 下落后刷新
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(st7735_init());
    fb_clear(COLOR_BG);
    fb_flush();
    xTaskCreate(game_task, "game_task", 4096, NULL, 5, NULL);
}