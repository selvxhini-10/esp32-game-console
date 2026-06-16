#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FLAPPY — tap-to-flap side-scroller.
 *
 * Unique mechanics vs the other three games:
 *   - Gravity simulation (bird accelerates downward constantly)
 *   - Flap on button press (impulse upward, resists gravity briefly)
 *   - Procedural pipe pairs scroll left at fixed speed
 *   - Score increments each time the bird clears a pipe pair
 *   - No joystick axis used — button only
 */

void  flappy_init(void);
void  flappy_input(int dx, int dy, bool btn);
bool  flappy_tick(uint32_t *score_out);
void  flappy_draw(void);

#ifdef __cplusplus
}
#endif