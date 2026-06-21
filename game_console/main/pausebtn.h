#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PAUSE BUTTON — dedicated pushbutton on GPIO26.
 *
 * Replaces the old "hold joystick button 1 second" pause gesture.
 * This button has exactly one job: open/request the pause screen.
 * It is wired with the internal pull-up enabled, so it reads LOW (0)
 * when pressed and HIGH (1) at rest — same active-low convention as
 * the joystick's click button.
 */

void pausebtn_init(void);

/*
 * Returns true exactly once per physical press (rising-edge-style
 * debounced read) — call this once per main loop iteration.
 * The console uses this to transition CON_PLAYING → CON_PAUSED.
 */
bool pausebtn_pressed(void);

#ifdef __cplusplus
}
#endif