#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5
#define PIN_NUM_DC   2
#define PIN_NUM_RST  4

static spi_device_handle_t oled;

void oled_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_NUM_DC, 0);

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd
    };

    spi_device_transmit(oled, &t);
}

void app_main(void)
{
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };

    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1
    };

    spi_bus_add_device(SPI2_HOST, &devcfg, &oled);

    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI("OLED", "SPI Initialized");

    while (1)
    {
        oled_cmd(0xAE);  // display off
        vTaskDelay(pdMS_TO_TICKS(1000));

        oled_cmd(0xAF);  // display on
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI("OLED", "Toggling display");
    }
}