#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "oled.h"
#include "joystick.h"
#include "nvs_scores.h"
#include "splash.h"
#include "console.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Game Console booting");

    /* ── Peripherals ── */
    nvs_scores_init();   /* must be first — NVS flash init */
    joystick_init();
    oled_init();

    /* ── Boot splash ── */
    splash_start();
    while (splash_tick()) {
        joystick_data_t j = joystick_read();
        /* Button press skips the splash */
        if (j.button == 0) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    /* ── Console ── */
    console_init();
    ESP_LOGI(TAG, "Entering console loop");

    while (1) {
        joystick_data_t j = joystick_read();

        int  joy_dx  = (j.x < 1000) ? -1 : (j.x > 3000) ?  1 : 0;
        int  joy_dy  = (j.y < 1000) ? -1 : (j.y > 3000) ?  1 : 0;
        bool joy_btn = (j.button == 0);   /* active-low, GPIO pull-up */

        console_input(joy_dx, joy_dy, joy_btn);
        console_tick();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}