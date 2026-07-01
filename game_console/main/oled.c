#if 0
#include "oled.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define PIN_MOSI 23
#define PIN_CLK  18
#define PIN_CS   5
#define PIN_DC   2
#define PIN_RST  4

static spi_device_handle_t oled_spi;

/* 128x64 = 1024 bytes (1 bit per pixel) */
static uint8_t framebuffer[OLED_WIDTH * OLED_HEIGHT / 8];

/* =========================
   LOW LEVEL HELPERS
   ========================= */

static void oled_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_DC, 0);

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd
    };

    spi_device_transmit(oled_spi, &t);
}

static void oled_data(const uint8_t *data, int len)
{
    gpio_set_level(PIN_DC, 1);

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data
    };

    spi_device_transmit(oled_spi, &t);
}

/* =========================
   PUBLIC API
   ========================= */

void oled_init(void)
{
    gpio_set_direction(PIN_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };

    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1
    };

    spi_bus_add_device(SPI2_HOST, &devcfg, &oled_spi);

    /* Reset display */
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Basic init sequence (SH1106/SSD1306 compatible subset) */
    oled_cmd(0xAE); // display off
    oled_cmd(0x20); // memory mode
    oled_cmd(0x10);
    oled_cmd(0xB0); // page 0
    oled_cmd(0xC8); // COM scan
    oled_cmd(0x00); // low col
    oled_cmd(0x10); // high col
    oled_cmd(0x40); // start line
    oled_cmd(0x81); // contrast
    oled_cmd(0x7F);
    oled_cmd(0xA1); // segment remap
    oled_cmd(0xA6); // normal display
    oled_cmd(0xA8); // multiplex
    oled_cmd(0x3F);
    oled_cmd(0xA4); // display RAM
    oled_cmd(0xD3); // offset
    oled_cmd(0x00);
    oled_cmd(0xD5); // clock
    oled_cmd(0x80);
    oled_cmd(0xD9); // precharge
    oled_cmd(0xF1);
    oled_cmd(0xDA); // COM pins
    oled_cmd(0x12);
    oled_cmd(0xDB); // VCOM detect
    oled_cmd(0x40);

    oled_cmd(0xAF); // display on

    memset(framebuffer, 0, sizeof(framebuffer));
}

/* =========================
   PIXEL DRAWING (BIT MANIPULATION)
   ========================= */

void oled_draw_pixel(int x, int y)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT)
        return;

    int index = x + (y / 8) * OLED_WIDTH;
    framebuffer[index] |= (1 << (y % 8));
}

/* =========================
   CLEAR SCREEN
   ========================= */
void oled_clear(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
}
/* =========================
   PUSH TO DISPLAY (SPI TRANSFER)
   ========================= */
void oled_update(void)
{
    /* SH1106-style full buffer send */

    for (int page = 0; page < 8; page++)
    {
        oled_cmd(0xB0 + page);   // set page
        oled_cmd(0x00);          // lower column
        oled_cmd(0x10);          // higher column

        gpio_set_level(PIN_DC, 1);

        spi_transaction_t t = {
            .length = 128 * 8,
            .tx_buffer = &framebuffer[page * 128]
        };

        spi_device_transmit(oled_spi, &t);
    }
}

/* =========================
   DRAW RECTANGLE
   ========================= */
void oled_draw_rect(int x, int y, int w, int h)
{
    for (int i = 0; i < w; i++)
    {
        oled_draw_pixel(x + i, y);
        oled_draw_pixel(x + i, y + h - 1);
    }

    for (int i = 0; i < h; i++)
    {
        oled_draw_pixel(x, y + i);
        oled_draw_pixel(x + w - 1, y + i);
    }
}
#endif