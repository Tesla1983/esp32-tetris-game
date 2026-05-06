#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tetris.h"

void app_main(void) {
    lv_init();

    tetris_start();
}
