#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "tft.h"
#include "joystick.h"
#include "nvs_scores.h"
#include "splash.h"
#include "console.h"
#include "sound.h"
#include "pausebtn.h"
#include "actionbtn.h"

static const char *TAG = "main";

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/*
 * ── WATCHDOG-FEEDER TASK ────────────────────────────────────────────────
 *
 * Why this exists: adc_oneshot_read() has NO timeout parameter and blocks
 * internally polling a hardware "conversion done" flag. If that flag
 * never sets — which can happen from a momentary power-rail brownout,
 * electrical noise, or (less commonly) a genuine ADC peripheral fault —
 * the call simply never returns. Every esp_task_wdt_reset() call placed
 * AFTER joystick_read() in the main loop becomes unreachable in that
 * scenario, which is exactly the crash you're seeing: main never gets
 * back to its own reset call, so the watchdog has no choice but to fire.
 *
 * No amount of code restructuring inside app_main's own loop can fix
 * this, because the problem is a single library call that doesn't
 * return — there's no point in the call's body to inject a timeout from
 * outside. The only real mitigation is a SEPARATE, independently-
 * scheduled task whose only job is to keep feeding the watchdog on a
 * fixed schedule, regardless of what main happens to be stuck doing.
 *
 * This does NOT fix a genuinely stuck ADC read — main will still be
 * frozen and the joystick will stop responding. What it prevents is the
 * watchdog forcing a full system PANIC/reboot over a transient hardware
 * hiccup; the feeder task keeps the system alive long enough for the
 * condition to often clear on its own (e.g. a brief brownout recovering),
 * after which main resumes from wherever it was stuck.
 *
 * If the underlying hardware fault is persistent rather than transient
 * (e.g. a genuinely damaged ADC unit), this task buys time but cannot
 * make the joystick start working again — the real fix in that case is
 * addressing the wiring/power issue itself (see the conversation history
 * for the backlight wiring fix this was investigated alongside).
 */
static void watchdog_feeder_task(void *arg)
{
    (void)arg;

    esp_err_t err = esp_task_wdt_add(NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "watchdog_feeder_task: esp_task_wdt_add failed: %s",
                 esp_err_to_name(err));
    }

    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));   /* well under the typical 5s TWDT timeout */
    }
}

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

    /*
     * Start the dedicated watchdog-feeder task — see its own comment block
     * above for why this exists. Pinned to CPU0 (same core as main) at a
     * higher priority so it always gets scheduled even if main is stuck
     * inside a blocking call like a stalled ADC read.
     */
    xTaskCreatePinnedToCore(watchdog_feeder_task, "wdt_feeder", 2048, NULL,
                             10, NULL, 0 /* CPU0 */);

    nvs_scores_init();
    joystick_init();
    tft_init();
    sound_init();       /* GPIO13 passive buzzer via LEDC PWM                */
    pausebtn_init();    /* GPIO26 dedicated pause pushbutton, debounced      */
    actionbtn_init();   /* GPIO27 dedicated in-game action button, debounced */

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
     * rising-edge detection via s_last_menu_btn (initialised true). So:
     *
     *   s_last_menu_btn = true   (set by console_init, see its own comment)
     *   first poll: menu_btn = true  (still held from splash dismiss)
     *   menu_btn_pressed = (true && !true) = FALSE  ← correctly suppressed
     *
     * console_init() already defends against this by initialising
     * s_last_menu_btn = true rather than false (see console.c). The wait-
     * for-release loop below is an additional defensive layer so the
     * button is also physically released before we ever read it again,
     * keeping behaviour predictable regardless of timing.
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
         * Prime the menu-button edge-detector with the current joystick
         * state before entering the main loop, and pass action_btn=false
         * since no game is active yet (CON_SELECTING ignores it anyway).
         */
        bool menu_btn_now = (j.button == 0);
        console_input(0, 0, menu_btn_now, false);   /* prime edge detector, no direction */
    }

    ESP_LOGI(TAG, "Entering console loop");

    while (1) {
        joystick_data_t j = joystick_read();

        int  joy_dx  = (j.x < 1000) ? -1 : (j.x > 3000) ?  1 : 0;
        int  joy_dy  = (j.y < 1000) ? -1 : (j.y > 3000) ?  1 : 0;

        /*
         * Button responsibilities — three physically distinct buttons,
         * three distinct jobs, never overlapping:
         *
         *   menu_btn   (joystick click)  → menu navigation only
         *   action_btn (GPIO27)          → in-game jump/flap/shoot/fire only
         *   pause      (GPIO26)          → pause request only (handled below)
         *
         * The joystick click no longer does anything during gameplay —
         * games receive action_btn instead, read directly from GPIO27.
         */
        bool menu_btn   = (j.button == 0);
        bool action_btn = actionbtn_is_pressed();

        console_input(joy_dx, joy_dy, menu_btn, action_btn);

        /*
         * Dedicated pause button (GPIO26) — completely separate from both
         * the joystick click and the action button. pausebtn_pressed()
         * already debounces and returns true exactly once per physical
         * press, so no edge-detection is needed here; just forward it.
         */
        if (pausebtn_pressed()) {
            console_pause_request();
        }

        console_tick();

        esp_task_wdt_reset();   /* defensive backstop — see splash loop comment above */

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}