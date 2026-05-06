#include "drivers/st7735.h"
#include "esp_check.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_HOST SPI2_HOST
#define PIN_NUM_MOSI 2
#define PIN_NUM_CLK 15
#define PIN_NUM_CS 17
#define PIN_NUM_DC 16
#define PIN_NUM_RST 4
#define PIN_NUM_BCKL 5

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

static const char *TAG = "st7735";
static st7735_dev_t g_lcd;

static inline esp_err_t st7735_send_cmd(uint8_t cmd) {
    gpio_set_level(PIN_NUM_DC, 0);
    spi_transaction_t t = {.length = 8, .tx_buffer = &cmd};
    return spi_device_transmit(g_lcd.spi, &t);
}

static inline esp_err_t st7735_send_data(const void *data, int len) {
    if (len <= 0) return ESP_OK;
    gpio_set_level(PIN_NUM_DC, 1);
    spi_transaction_t t = {.length = len * 8, .tx_buffer = data};
    return spi_device_transmit(g_lcd.spi, &t);
}

static void st7735_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    st7735_send_cmd(ST7735_CMD_CASET);
    uint8_t data[4] = {(x0 >> 8) & 0xFF, x0 & 0xFF, (x1 >> 8) & 0xFF, x1 & 0xFF};
    st7735_send_data(data, 4);

    st7735_send_cmd(ST7735_CMD_RASET);
    data[0] = (y0 >> 8) & 0xFF;
    data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF;
    data[3] = y1 & 0xFF;
    st7735_send_data(data, 4);

    st7735_send_cmd(ST7735_CMD_RAMWR);
}

esp_err_t st7735_draw_frame(const uint16_t *framebuffer, int width, int height) {
    if (!framebuffer || width != ST7735_H_RES || height != ST7735_V_RES) {
        return ESP_ERR_INVALID_ARG;
    }
    st7735_set_window(0, 0, ST7735_H_RES - 1, ST7735_V_RES - 1);
    return st7735_send_data((const uint8_t *)framebuffer, ST7735_H_RES * ST7735_V_RES * 2);
}

esp_err_t st7735_init(void) {
    spi_bus_config_t buscfg = {.mosi_io_num = PIN_NUM_MOSI,
                               .miso_io_num = -1,
                               .sclk_io_num = PIN_NUM_CLK,
                               .quadwp_io_num = -1,
                               .quadhd_io_num = -1,
                               .max_transfer_sz = ST7735_H_RES * ST7735_V_RES * 2 + 8};

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 25 * 1000 * 1000, .mode = 0, .spics_io_num = PIN_NUM_CS, .queue_size = 2};

    ESP_ERROR_CHECK(gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT));

    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus init failed");
    ESP_RETURN_ON_ERROR(spi_bus_add_device(LCD_HOST, &devcfg, &g_lcd.spi), TAG, "spi add device failed");

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

    uint8_t madctl = 0xC0;
    st7735_send_cmd(ST7735_CMD_MADCTL);
    st7735_send_data(&madctl, 1);

    uint8_t colmod = 0x05;
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
    ESP_LOGI(TAG, "ST7735 init done (%dx%d)", ST7735_H_RES, ST7735_V_RES);
    return ESP_OK;
}
