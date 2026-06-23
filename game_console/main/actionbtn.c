/*
 * ACTION BUTTON — GPIO27 pushbutton driver with debounce.
 *
 * Same debounce strategy as pausebtn.c: mechanical buttons bounce for a
 * few milliseconds on press/release, so raw level reads in a tight poll
 * loop can register noise as multiple rapid presses. A short time-based
 * debounce window fixes this with minimal code, matching the existing
 * pattern already established by pausebtn.c for consistency across the
 * project's two dedicated-button drivers.
 */

#include "actionbtn.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define ACTION_BTN_GPIO   GPIO_NUM_27
#define DEBOUNCE_MS       20      /* slightly shorter than pausebtn's 30ms —
                                     action button needs to feel snappier
                                     for time-critical inputs like jumping */

static bool    s_stable_pressed = false;   /* debounced state: true = held down */
static bool    s_last_raw       = true;    /* true = released (pulled high)     */
static int64_t s_last_change_ms = 0;

static inline int64_t ab_now(void) { return esp_timer_get_time() / 1000; }

void actionbtn_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << ACTION_BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,     /* polled, not interrupt-driven */
    };
    gpio_config(&cfg);

    bool raw_released   = (gpio_get_level(ACTION_BTN_GPIO) != 0);
    s_last_raw           = raw_released;
    s_stable_pressed     = !raw_released;
    s_last_change_ms     = ab_now();
}

bool actionbtn_is_pressed(void)
{
    bool raw_released = (gpio_get_level(ACTION_BTN_GPIO) != 0);   /* 1=released, 0=pressed */
    int64_t now = ab_now();

    if (raw_released != s_last_raw) {
        /* Raw level changed — start/continue the debounce window */
        s_last_raw       = raw_released;
        s_last_change_ms = now;
    } else if ((now - s_last_change_ms) >= DEBOUNCE_MS) {
        /* Level has been stable for long enough — accept it */
        s_stable_pressed = !raw_released;
    }

    return s_stable_pressed;
}