#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "oled.h"
#include "joystick.h"

void app_main(void)
{
    joystick_init();
    oled_init();

    int x = 64;
    int y = 32;

    while (1)
    {
        joystick_data_t j = joystick_read();

        if (j.x < 1000) x--;
        if (j.x > 3000) x++;

        if (j.y < 1000) y--;
        if (j.y > 3000) y++;

        oled_clear();
        oled_draw_rect(x, y, 4, 4);
        oled_update();

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}