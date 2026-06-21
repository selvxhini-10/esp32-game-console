/*
 * PAUSE BUTTON — GPIO26 pushbutton driver with debounce.
 *
 * Why a separate debounced edge-detector module instead of reading the
 * raw GPIO level directly in app_main: mechanical buttons "bounce" —
 * the contact briefly oscillates open/closed over a few milliseconds
 * when pressed or released. Reading the raw level in a tight loop can
 * register one physical press as several rapid presses. A simple
 * time-based debounce (ignore state changes within a short window of
 * the last accepted change) fixes this with almost no code.
 */

#include "pausebtn.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define PAUSE_BTN_GPIO   GPIO_NUM_26
#define DEBOUNCE_MS      30      /* ignore bounces within 30ms of last change */

static bool    s_last_stable_state = true;   /* true = released (pulled high) */
static int64_t s_last_change_ms    = 0;

static inline int64_t pb_now(void) { return esp_timer_get_time() / 1000; }

void pausebtn_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PAUSE_BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,    /* button shorts to GND when pressed */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,     /* polled, not interrupt-driven */
    };
    gpio_config(&cfg);

    s_last_stable_state = (gpio_get_level(PAUSE_BTN_GPIO) != 0);
    s_last_change_ms    = pb_now();
}

bool pausebtn_pressed(void)
{
    bool raw_released = (gpio_get_level(PAUSE_BTN_GPIO) != 0);   /* 1=released, 0=pressed */
    int64_t now = pb_now();

    /* Debounce: only accept a state change if enough time has passed */
    if (raw_released != s_last_stable_state) {
        if ((now - s_last_change_ms) >= DEBOUNCE_MS) {
            bool was_released = s_last_stable_state;
            s_last_stable_state = raw_released;
            s_last_change_ms    = now;

            /* Rising-edge-equivalent: fire only on released→pressed transition */
            if (was_released && !raw_released) {
                return true;
            }
        }
    } else {
        /* State matches last stable reading — reset the change timer so a
         * genuinely held button doesn't accidentally "debounce in" later   */
        s_last_change_ms = now;
    }

    return false;
}