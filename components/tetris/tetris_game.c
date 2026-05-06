#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "drivers/st7735.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tetris.h"


static const char *TAG = "TETRIS";

// 双缓冲 framebuffer：渲染一块，DMA 发送另一块
static uint16_t framebuffers[2][ST7735_V_RES][ST7735_H_RES];
static int fb_idx = 0;  // 当前渲染缓冲区索引
#define FB_CURRENT framebuffers[fb_idx]
#define FB_OTHER   framebuffers[fb_idx ^ 1]

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color_be) {
    if (x >= ST7735_H_RES || y >= ST7735_V_RES) return;
    if (x + w > ST7735_H_RES) w = ST7735_H_RES - x;
    if (y + h > ST7735_V_RES) h = ST7735_V_RES - y;
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) FB_CURRENT[y + i][x + j] = color_be;
    }
}

static inline void fb_clear(uint16_t color_be) {
    for (int y = 0; y < ST7735_V_RES; ++y) {
        for (int x = 0; x < ST7735_H_RES; ++x) FB_CURRENT[y][x] = color_be;
    }
}

// ==================== 帧数监控结构 ====================
typedef struct {
    int64_t start_time;          // 监控周期开始时间 (微秒)
    uint32_t frame_count;        // 帧计数
    uint32_t current_fps;        // 当前 FPS
    uint32_t max_fps;            // 最大 FPS
    uint32_t min_fps;            // 最小 FPS
    uint64_t total_frames;       // 总帧数
    uint64_t total_render_time;  // 总渲染耗时 (微秒)
    uint32_t max_frame_time;     // 单帧最大耗时 (微秒)
    uint32_t min_frame_time;     // 单帧最小耗时 (微秒)
} fps_monitor_t;

static fps_monitor_t fps_monitor = {.start_time = 0,
                                    .frame_count = 0,
                                    .current_fps = 0,
                                    .max_fps = 0,
                                    .min_fps = UINT32_MAX,
                                    .total_frames = 0,
                                    .total_render_time = 0,
                                    .max_frame_time = 0,
                                    .min_frame_time = UINT32_MAX};

static int64_t render_start_time = 0;

/**
 * @brief 初始化帧数监控
 */
static void fps_monitor_init(void) {
    fps_monitor.start_time = esp_timer_get_time();
    fps_monitor.frame_count = 0;
    fps_monitor.current_fps = 0;
    fps_monitor.max_fps = 0;
    fps_monitor.min_fps = UINT32_MAX;
    fps_monitor.total_frames = 0;
    fps_monitor.total_render_time = 0;
    fps_monitor.max_frame_time = 0;
    fps_monitor.min_frame_time = UINT32_MAX;
    ESP_LOGI(TAG, "FPS Monitor initialized (Display: %d x %d, Total bytes/frame: %d)", ST7735_H_RES, ST7735_V_RES,
             ST7735_H_RES * ST7735_V_RES * 2);
}

/**
 * @brief 标记渲染开始
 */
static inline void fps_render_start(void) { render_start_time = esp_timer_get_time(); }

/**
 * @brief 标记渲染结束并更新统计
 */
static void fps_render_end(void) {
    int64_t current_time = esp_timer_get_time();
    uint32_t frame_time = (uint32_t)(current_time - render_start_time);

    fps_monitor.frame_count++;
    fps_monitor.total_frames++;
    fps_monitor.total_render_time += frame_time;

    // 更新单帧最大和最小耗时
    if (frame_time > fps_monitor.max_frame_time) {
        fps_monitor.max_frame_time = frame_time;
    }
    if (frame_time < fps_monitor.min_frame_time) {
        fps_monitor.min_frame_time = frame_time;
    }

    int64_t elapsed = current_time - fps_monitor.start_time;

    // 每1秒输出一次统计
    if (elapsed >= 1000000) {  // 1秒 = 1000000微秒
        fps_monitor.current_fps = (uint32_t)(fps_monitor.frame_count * 1000000 / elapsed);
        uint32_t avg_frame_time = (uint32_t)(fps_monitor.total_render_time / fps_monitor.frame_count);

        // 更新最大和最小FPS
        if (fps_monitor.current_fps > fps_monitor.max_fps) {
            fps_monitor.max_fps = fps_monitor.current_fps;
        }
        if (fps_monitor.current_fps < fps_monitor.min_fps && fps_monitor.current_fps > 0) {
            fps_monitor.min_fps = fps_monitor.current_fps;
        }

        // 输出详细的性能统计
        ESP_LOGI(TAG,
                 "当前FPS: %lu | 最大FPS: %lu | 最小FPS: %lu | 平均帧时间: %lu us | 最大帧时间: %lu us | 总帧数: %llu",
                 fps_monitor.current_fps, fps_monitor.max_fps, fps_monitor.min_fps, avg_frame_time,
                 fps_monitor.max_frame_time, fps_monitor.total_frames);

        // 重置计数器
        fps_monitor.frame_count = 0;
        fps_monitor.total_render_time = 0;
        fps_monitor.max_frame_time = 0;
        fps_monitor.min_frame_time = UINT32_MAX;
        fps_monitor.start_time = current_time;
    }
}

static void fb_flush(void) {
    fps_render_start();
    // 先等上一帧 DMA 完成（此时渲染与 DMA 已重叠）
    st7735_wait_frame();
    // 启动当前帧的异步 DMA 传输
    st7735_draw_frame_async((const uint16_t *)FB_CURRENT);
    fps_render_end();
    // 交换缓冲：下一帧渲染到另一块
    fb_idx ^= 1;
    // 更新 LVGL 的绘制缓冲区指向新的当前缓冲区
    lv_display_set_buffers(lv_disp_get_default(), FB_CURRENT, NULL,
                           sizeof(FB_CURRENT), LV_DISPLAY_RENDER_MODE_DIRECT);
}

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

static int clear_lines(void) {
    int cleared = 0;
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
            cleared++;
            y++;
        }
    }
    return cleared;
}

// ==================== LVGL 分数显示 ====================
static int score = 0;
static lv_obj_t *score_label = NULL;

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    // px_map 即 FB_CURRENT，由 fb_flush 统一管理异步 DMA + 缓冲切换
    fb_flush();
    lv_display_flush_ready(disp);
}

static void lvgl_score_init(void) {
    lv_display_t *disp = lv_display_create(ST7735_H_RES, ST7735_V_RES);
    lv_display_set_default(disp);
    lv_display_set_buffers(disp, FB_CURRENT, NULL, sizeof(FB_CURRENT),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    // 屏幕背景透明，让游戏画面透出
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_TRANSP, 0);

    score_label = lv_label_create(lv_scr_act());
    lv_label_set_text(score_label, "000");
    lv_obj_set_style_text_color(score_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(score_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(score_label, 2, 2);

    // 强制渲染初始 "000" 到 framebuffer 并刷屏
    lv_refr_now(NULL);
}

static void lvgl_update_score(void) {
    // 更新标签文本，%03d 保持三位数显示（如 000, 010, 999）
    lv_label_set_text_fmt(score_label, "%03d", score);
    lv_obj_invalidate(score_label);
    // 由 LVGL 统一刷屏（含游戏画面 + 分数），消除闪烁
    lv_refr_now(NULL);
}

static void spawn_piece(void) {
    cur.type = esp_random() % 7;
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
    // fb_flush() 已移除：由 lvgl_update_score() 中的 lv_refr_now() 统一刷屏
}

static int fall_speed = 6;
static int64_t last_game_update = 0;  // 游戏逻辑的最后更新时间

static void game_task(void *arg) {
    (void)arg;

    fps_monitor_init();
    last_game_update = esp_timer_get_time();

    memset(board, 0, sizeof(board));
    spawn_piece();
    score = 0;
    lvgl_score_init();  // 显示 "000" 并刷屏
    render();           // 绘制游戏画面到 framebuffer
    lvgl_update_score();// LVGL 叠加上分数并统一刷屏

    int tick = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        // 游戏逻辑更新间隔：50ms 更新一次（20 Hz）
        if ((now - last_game_update) >= 50000) {  // 50ms = 50000微秒
            last_game_update = now;

            if ((tick % 30) == 0) {  // 每30个tick（即1.5秒）旋转一次
                int test_r = (cur.rot + 3) & 3;
                if (!collision(&cur, cur.x, cur.y, test_r)) cur.rot = test_r;
            }
            if ((tick % 10) == 0) {  // 每10个tick（即0.5秒）左右移动一次
                int dir = ((tick / 10) & 1) ? 1 : -1;
                int test_x = cur.x + dir;
                if (!collision(&cur, test_x, cur.y, cur.rot)) cur.x = test_x;
            }
            if ((tick % fall_speed) == 0) {  // 每fall_speed个tick（即fall_speed*50ms）下降一次
                if (!collision(&cur, cur.x, cur.y + 1, cur.rot))
                    cur.y++;
                else {
                    merge_current_piece();
                    int cleared = clear_lines();
                    if (cleared > 0) {
                        score += cleared * 10;
                    }
                    spawn_piece();
                }
            }
            tick++;
        }

        // 渲染尽可能快地执行（不受vTaskDelay限制）
        render();
        // 使用 LVGL 在左上角显示分数
        lvgl_update_score();
        // 每100微秒让出一次CPU，避免完全占用
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void tetris_start(void) {
    ESP_ERROR_CHECK(st7735_init());
    fb_clear(COLOR_BG);
    // 初始刷屏：同步等待 + 异步发送
    st7735_wait_frame();
    st7735_draw_frame_async((const uint16_t *)FB_CURRENT);
    st7735_wait_frame();
    xTaskCreate(game_task, "game_task", 40960, NULL, 5, NULL);
}
