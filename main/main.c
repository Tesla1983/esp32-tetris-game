#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tetris.h"

void app_main(void) {
    lv_init();
    lv_tick_set_cb(NULL);

    tetris_start();
}
