#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "oled.h"
#include "menu.h"
#include "joystick.h"

void app_main(void) {
    joystick_init();
    oled_init();
    menu_init();

    int x = 60;
    int y = 28;
    const int DINO_W = 4;
    const int DINO_H = 4;

    while (1) {
        joystick_data_t j = joystick_read();
        int joy_dy = (j.y < 1000) ? -1 : (j.y > 3000) ? 1 : 0;
        bool joy_btn = (j.button == 0); // active low; pulled-up GPIO reads 0 when pressed

        menu_input(joy_dy, joy_btn);

        if (menu_get_state() == MENU_STATE_GAME) {
            /* Move player */
            if (j.x < 1000) x--;
            if (j.x > 3000) x++;
            y += joy_dy;

            /* Clamp rect to display bounds using collision helper */
            Rect player = {x, y, DINO_W, DINO_H};
            rect_clamp_to_bounds(&player);
            x = player.x;
            y = player.y;

            /* Draw game frame */
            oled_clear();
            oled_draw_rect(x, y, DINO_W, DINO_H);
            oled_update();
        } else {
            menu_draw(); /* menu owns clear + update */
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}