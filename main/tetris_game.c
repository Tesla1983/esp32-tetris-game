#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "drivers/st7735.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BOARD_W 10
#define BOARD_H 13
#define CELL_SIZE 12
#define OFFSET_X (((ST7735_H_RES - BOARD_W * CELL_SIZE) / 2))
#define OFFSET_Y (((ST7735_V_RES - BOARD_H * CELL_SIZE) / 2))

#define COLOR_BG 0xFFFF
#define COLOR_GRID 0xCE79
#define COLOR_SCORE 0x18C6
#define COLOR_FLASH 0x7BEF

static const uint16_t piece_colors[7] = {0x07FF, 0xFFE0, 0xF81F, 0x07E0, 0xF800, 0x001F, 0xFD20};
static uint8_t board[BOARD_H][BOARD_W];
static uint16_t cell_cache[BOARD_H][BOARD_W];

typedef struct {
    int type;
    int rot;
    int x;
    int y;
} piece_t;
static piece_t cur;

typedef struct {
    uint8_t snapshot[BOARD_H][BOARD_W];
    piece_t piece;
    int score;
    int flash_ticks;
    bool full_refresh;
} render_state_t;

static QueueHandle_t render_queue;

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

static int clear_lines(void) {
    int cleared = 0;
    for (int y = BOARD_H - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < BOARD_W; ++x)
            if (!board[y][x]) full = false;
        if (full) {
            for (int yy = y; yy > 0; --yy) memcpy(board[yy], board[yy - 1], BOARD_W);
            memset(board[0], 0, BOARD_W);
            ++cleared;
            ++y;
        }
    }
    return cleared;
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

static uint16_t cell_color(const render_state_t *st, int x, int y) {
    uint8_t v = st->snapshot[y][x];
    uint16_t color = v ? piece_colors[v - 1] : COLOR_BG;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (tetromino[st->piece.type][st->piece.rot][r][c]) {
                int px = st->piece.x + c, py = st->piece.y + r;
                if (px == x && py == y) color = piece_colors[st->piece.type];
            }
    if (st->flash_ticks > 0 && ((st->flash_ticks & 1) == 0) && !v) color = COLOR_FLASH;
    return color;
}

static void render_cell_dma(int bx, int by, uint16_t color) {
    static uint16_t tile[CELL_SIZE * CELL_SIZE];
    for (int y = 0; y < CELL_SIZE; ++y)
        for (int x = 0; x < CELL_SIZE; ++x)
            tile[y * CELL_SIZE + x] = (x == CELL_SIZE - 1 || y == CELL_SIZE - 1) ? COLOR_GRID : color;
    ESP_ERROR_CHECK(st7735_draw_region_dma(OFFSET_X + bx * CELL_SIZE, OFFSET_Y + by * CELL_SIZE, CELL_SIZE,
                                           CELL_SIZE, tile));
}

static void render_score(int score) {
    static const uint8_t digits[10][5] = {{0x7,0x5,0x5,0x5,0x7},{0x2,0x6,0x2,0x2,0x7},{0x7,0x1,0x7,0x4,0x7},{0x7,0x1,0x7,0x1,0x7},{0x5,0x5,0x7,0x1,0x1},{0x7,0x4,0x7,0x1,0x7},{0x7,0x4,0x7,0x5,0x7},{0x7,0x1,0x1,0x1,0x1},{0x7,0x5,0x7,0x5,0x7},{0x7,0x5,0x7,0x1,0x7}};
    static uint16_t buf[36 * 18];
    const int w = 36, h = 18, x0 = ST7735_H_RES - w - 4, y0 = 4;
    for (int i = 0; i < w * h; ++i) buf[i] = COLOR_BG;
    int v = score % 1000;
    for (int d = 0; d < 3; ++d) {
        int dig = (v / (d == 0 ? 100 : (d == 1 ? 10 : 1))) % 10;
        for (int ry = 0; ry < 5; ++ry)
            for (int rx = 0; rx < 3; ++rx)
                if (digits[dig][ry] & (1 << (2 - rx)))
                    for (int sy = 0; sy < 3; ++sy)
                        for (int sx = 0; sx < 3; ++sx)
                            buf[(2 + ry * 3 + sy) * w + (d * 12 + 2 + rx * 3 + sx)] = COLOR_SCORE;
    }
    ESP_ERROR_CHECK(st7735_draw_region_dma(x0, y0, w, h, buf));
}

static void render_task(void *arg) {
    (void)arg;
    memset(cell_cache, 0xFF, sizeof(cell_cache));
    render_state_t st;
    while (xQueueReceive(render_queue, &st, portMAX_DELAY) == pdTRUE) {
        for (int y = 0; y < BOARD_H; ++y)
            for (int x = 0; x < BOARD_W; ++x) {
                uint16_t color = cell_color(&st, x, y);
                if (st.full_refresh || cell_cache[y][x] != color) {
                    render_cell_dma(x, y, color);
                    cell_cache[y][x] = color;
                }
            }
        render_score(st.score);
    }
}

static void game_task(void *arg) {
    (void)arg;
    int score = 0, flash_ticks = 0;
    memset(board, 0, sizeof(board));
    spawn_piece();
    render_state_t st = {.score = score, .flash_ticks = flash_ticks, .full_refresh = true};
    memcpy(st.snapshot, board, sizeof(board));
    st.piece = cur;
    xQueueSend(render_queue, &st, portMAX_DELAY);

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
        if ((tick % 6) == 0) {
            if (!collision(&cur, cur.x, cur.y + 1, cur.rot)) {
                cur.y++;
            } else {
                merge_current_piece();
                int lines = clear_lines();
                if (lines > 0) {
                    score += lines * 10;
                    flash_ticks = 6;
                }
                spawn_piece();
            }
        }
        if (flash_ticks > 0) flash_ticks--;
        st.score = score;
        st.flash_ticks = flash_ticks;
        st.full_refresh = false;
        memcpy(st.snapshot, board, sizeof(board));
        st.piece = cur;
        xQueueOverwrite(render_queue, &st);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(st7735_init());
    render_queue = xQueueCreate(1, sizeof(render_state_t));
    xTaskCreate(render_task, "render_task", 4096, NULL, 6, NULL);
    xTaskCreate(game_task, "game_task", 4096, NULL, 5, NULL);
}
