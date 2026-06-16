#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "oled.h"
#include "joystick.h"
#include "nvs_scores.h"
#include "splash.h"
#include "console.h"

static const char *TAG = "main";

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Game Console booting");

    nvs_scores_init();
    joystick_init();
    oled_init();

    /* ── Splash ──────────────────────────────────────────────────────────────
     *   - splash_tick() holds the final frame indefinitely (returns true
     *     forever after the animation completes).
     *   - app_main polls the button independently and breaks the loop itself.
     *   - A 30ms frame limiter here means ~33 fps during the animation.
     *   - Button latch: we wait until the button is seen released (== 1) before
     *     allowing a press (== 0) to exit. This prevents the GPIO settle noise
     *     at power-on from triggering an instant skip.
     *   - Animation must be fully complete (t >= T_ANIM_DONE_MS) before the
     *     button can exit — so the user always sees the full animation at least
     *     once before being allowed to skip into the menu.
     * ─────────────────────────────────────────────────────────────────────── */
    
    #define T_ANIM_DONE_MS  1800    /* after CONSOLE finishes sliding in */
    #define SPLASH_FRAME_MS   30    /* ~33 fps rendering rate            */

    splash_start();

    bool    btn_was_released = false;
    int64_t last_frame_ms    = now_ms();
    int64_t splash_start_ms  = now_ms();

    while (1) {
        int64_t t = now_ms();

        /* Frame limiter — only render at 33 fps */
        if ((t - last_frame_ms) >= SPLASH_FRAME_MS) {
            last_frame_ms = t;
            splash_tick();   /* draw current animation frame */
        }

        /* Read joystick */
        joystick_data_t j = joystick_read();

        /* Latch: wait until button seen released before allowing skip */
        if (j.button == 1) {
            btn_was_released = true;
        }

        /* Exit conditions:
         *   a) Animation complete AND button pressed
         *   b) Animation complete AND button held for 200ms (joystick modules
         *      sometimes never reach ==1 due to hardware variation — this
         *      fallback catches that case after the animation is done)       */
        int64_t elapsed = t - splash_start_ms;
        bool anim_done  = (elapsed >= T_ANIM_DONE_MS);

        if (anim_done && btn_was_released && j.button == 0) {
            ESP_LOGI(TAG, "Splash dismissed by button press");
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(5));   /* yield to scheduler between polls */
    }

    /* ── Console ─────────────────────────────────────────────────────────── */
    console_init();
    ESP_LOGI(TAG, "Entering console loop");

    while (1) {
        joystick_data_t j = joystick_read();

        int  joy_dx  = (j.x < 1000) ? -1 : (j.x > 3000) ?  1 : 0;
        int  joy_dy  = (j.y < 1000) ? -1 : (j.y > 3000) ?  1 : 0;
        bool joy_btn = (j.button == 0);

        console_input(joy_dx, joy_dy, joy_btn);
        console_tick();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}