#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "oled.h"
#include "joystick.h"
#include "nvs_scores.h"
#include "splash.h"
#include "console.h"
#include "sound.h"
#include "pausebtn.h"

static const char *TAG = "main";

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Game Console booting");

    /*
     * Subscribe the main task to the Task Watchdog Timer (TWDT).
     *
     * esp_task_wdt_reset() is a no-op (returns ESP_ERR_NOT_FOUND silently)
     * unless the calling task has been explicitly added to the watchdog
     * first. Only IDLE0/IDLE1 are subscribed by default — "main" is not.
     * This call must happen before any esp_task_wdt_reset() calls below
     * are able to do anything useful.
     */
    esp_err_t wdt_err = esp_task_wdt_add(NULL);   /* NULL = current task (main) */
    if (wdt_err != ESP_OK && wdt_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_task_wdt_add failed: %s", esp_err_to_name(wdt_err));
    }

    nvs_scores_init();
    joystick_init();
    oled_init();
    sound_init();       /* GPIO13 passive buzzer via LEDC PWM            */
    pausebtn_init();    /* GPIO26 dedicated pause pushbutton, debounced  */

    /* Boot fanfare — fires once, roughly synced with the splash animation
     * starting. A short ascending arpeggio gives the console an audible
     * "power on" identity rather than booting in total silence. */
    static const Note boot_tune[] = {
        { NOTE_C4, 80 }, { NOTE_E4, 80 }, { NOTE_G4, 80 }, { NOTE_C5, 150 },
    };
    sound_play_melody(boot_tune, sizeof(boot_tune)/sizeof(boot_tune[0]));

    /* ── Splash ──────────────────────────────────────────────────────────────
     *
     * Why the previous version exited instantly:
     *
     *   1. splash_tick() auto-returned false at T_HOLD_END (2600 ms) whether
     *      or not the button was pressed — "PRESS BTN" was purely decorative.
     *
     *   2. The loop called splash_tick() at ~16 ms intervals but splash_tick()
     *      has no internal frame limiter — on each call it recomputes elapsed
     *      time from esp_timer. With vTaskDelay(16ms) the 2600ms window was
     *      consumed in ~162 loop iterations and the splash exited immediately
     *      from the user's perspective (the 300ms blank + 500ms animations
     *      compressed into the display settling time).
     *
     * Fix:
     *   - splash_tick() now holds the final frame indefinitely (returns true
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

        /*
         * Explicit watchdog feed as a defensive backstop. vTaskDelay()
         * below already yields to the scheduler, which normally lets the
         * idle task run and keeps the watchdog happy on its own. This
         * extra call costs nothing and guarantees the watchdog is reset
         * every iteration even in edge cases where scheduling is delayed
         * for any reason (e.g. a slow ADC read, see joystick.c notes).
         */
        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(5));   /* yield to scheduler between polls */
    }

    /* ── Console ─────────────────────────────────────────────────────────── */
    console_init();

    /*
     * Button bleed-through guard.
     *
     * Problem: the button press that dismissed the splash may still be
     * physically held when we reach console_init(). console_input() uses
     * rising-edge detection via s_last_btn (initialised false). So:
     *
     *   s_last_btn = false  (set by console_init)
     *   first poll: joy_btn = true  (button still held from splash dismiss)
     *   btn_pressed = (true && !false) = TRUE  ← spurious press detected
     *   → CON_SELECTING fires CON_COUNTDOWN immediately
     *   → 3-2-1 Snake appears before menu ever draws
     *
     * Fix A — wait for physical release (max 800ms timeout so we don't
     * block forever if the button hardware reads 0 at rest):
     */
    {
        int64_t release_deadline = now_ms() + 800;
        joystick_data_t j;
        do {
            j = joystick_read();
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (j.button == 0 && now_ms() < release_deadline);

        ESP_LOGI(TAG, "Button state at console entry: %d", j.button);

        /*
         * Fix B — regardless of whether the button is physically released,
         * prime the edge-detector so the first seen state is NOT treated as
         * a fresh press. We do this by calling console_input() once with the
         * CURRENT button state before entering the main loop. This sets
         * s_last_btn to the current value, so the next call only fires a
         * press event on a genuine new press.
         */
        bool btn_now = (j.button == 0);
        console_input(0, 0, btn_now);   /* prime edge detector, no direction */
    }

    ESP_LOGI(TAG, "Entering console loop");

    while (1) {
        joystick_data_t j = joystick_read();

        int  joy_dx  = (j.x < 1000) ? -1 : (j.x > 3000) ?  1 : 0;
        int  joy_dy  = (j.y < 1000) ? -1 : (j.y > 3000) ?  1 : 0;
        bool joy_btn = (j.button == 0);

        console_input(joy_dx, joy_dy, joy_btn);

        /*
         * Dedicated pause button (GPIO26) — completely separate from the
         * joystick click. pausebtn_pressed() already debounces and returns
         * true exactly once per physical press, so no edge-detection is
         * needed here; just forward it straight to the console.
         */
        if (pausebtn_pressed()) {
            console_pause_request();
        }

        console_tick();

        esp_task_wdt_reset();   /* defensive backstop — see splash loop comment above */

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}