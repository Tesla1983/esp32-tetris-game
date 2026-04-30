#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "drivers/st7735.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static uint16_t framebuffer[ST7735_V_RES][ST7735_H_RES];

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color_be) {
    if (x >= ST7735_H_RES || y >= ST7735_V_RES) return;
    if (x + w > ST7735_H_RES) w = ST7735_H_RES - x;
    if (y + h > ST7735_V_RES) h = ST7735_V_RES - y;
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) framebuffer[y + i][x + j] = color_be;
    }
}

static inline void fb_clear(uint16_t color_be) {
    for (int y = 0; y < ST7735_V_RES; ++y) {
        for (int x = 0; x < ST7735_H_RES; ++x) framebuffer[y][x] = color_be;
    }
}

static void fb_flush(void) { ESP_ERROR_CHECK(st7735_draw_frame((uint16_t *)framebuffer, ST7735_H_RES, ST7735_V_RES)); }

#define BOARD_W 10
#define BOARD_H 13
#define CELL_SIZE 12
#define OFFSET_X (((ST7735_H_RES - BOARD_W * CELL_SIZE) / 2))
#define OFFSET_Y (((ST7735_V_RES - BOARD_H * CELL_SIZE) / 2))

#define COLOR_BG 0xFFFF
#define COLOR_GRID 0xCE79

static const uint16_t piece_colors[7] = {0x07FF, 0xFFE0, 0xF81F, 0x07E0, 0xF800, 0x001F, 0xFD20};
static uint8_t board[BOARD_H][BOARD_W];

typedef struct {
    int type;
    int rot;
    int x;
    int y;
} piece_t;
static piece_t cur;

static const uint8_t tetromino[7][4][4][4] = {
    {{{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}},
    {{{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
    {{{0, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
    {{{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}}},
    {{{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
    {{{1, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}}},
    {{{0, 0, 1, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}},
     {{1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
};

static void draw_cell(int bx, int by, uint16_t color) {
    int x = OFFSET_X + bx * CELL_SIZE, y = OFFSET_Y + by * CELL_SIZE;
    fb_fill_rect(x, y, CELL_SIZE - 1, CELL_SIZE - 1, color);
    fb_fill_rect(x + CELL_SIZE - 1, y, 1, CELL_SIZE, COLOR_GRID);
    fb_fill_rect(x, y + CELL_SIZE - 1, CELL_SIZE, 1, COLOR_GRID);
}

static bool collision(const piece_t *p, int nx, int ny, int nrot) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (tetromino[p->type][nrot][r][c]) {
                int x = nx + c, y = ny + r;
                if (x < 0 || x >= BOARD_W || y >= BOARD_H) return true;
                if (y >= 0 && board[y][x]) return true;
            }
    return false;
}

static void merge_current_piece(void) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (tetromino[cur.type][cur.rot][r][c]) {
                int x = cur.x + c, y = cur.y + r;
                if (x >= 0 && x < BOARD_W && y >= 0 && y < BOARD_H) board[y][x] = (uint8_t)(cur.type + 1);
            }
}

static void clear_lines(void) {
    for (int y = BOARD_H - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < BOARD_W; ++x)
            if (!board[y][x]) {
                full = false;
                break;
            }
        if (full) {
            for (int yy = y; yy > 0; --yy) memcpy(board[yy], board[yy - 1], BOARD_W);
            memset(board[0], 0, BOARD_W);
            y++;
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
    if (collision(&cur, cur.x, cur.y, cur.rot)) memset(board, 0, sizeof(board));
}

static void render(void) {
    fb_clear(COLOR_BG);
    for (int y = 0; y < BOARD_H; ++y)
        for (int x = 0; x < BOARD_W; ++x)
            if (board[y][x]) draw_cell(x, y, piece_colors[board[y][x] - 1]);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (tetromino[cur.type][cur.rot][r][c]) {
                int x = cur.x + c, y = cur.y + r;
                if (x >= 0 && x < BOARD_W && y >= 0 && y < BOARD_H) draw_cell(x, y, piece_colors[cur.type]);
            }
    fb_flush();
}

static int fall_speed = 6;

static void game_task(void *arg) {
    (void)arg;
    memset(board, 0, sizeof(board));
    spawn_piece();
    render();
    for (int tick = 0;; ++tick) {
        if ((tick % 30) == 0) {
            int test_r = (cur.rot + 1) & 3;
            if (!collision(&cur, cur.x, cur.y, test_r)) cur.rot = test_r;
        }
        if ((tick % 10) == 0) {
            int dir = ((tick / 10) & 1) ? 1 : -1;
            int test_x = cur.x + dir;
            if (!collision(&cur, test_x, cur.y, cur.rot)) cur.x = test_x;
        }
        if ((tick % fall_speed) == 0) {
            if (!collision(&cur, cur.x, cur.y + 1, cur.rot))
                cur.y++;
            else {
                merge_current_piece();
                clear_lines();
                spawn_piece();
            }
            render();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(st7735_init());
    fb_clear(COLOR_BG);
    fb_flush();
    xTaskCreate(game_task, "game_task", 4096, NULL, 5, NULL);
}
