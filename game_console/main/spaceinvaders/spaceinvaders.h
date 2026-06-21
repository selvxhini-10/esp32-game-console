#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SPACE INVADERS
 *
 * Mechanics:
 *   - 4×8 grid of aliens (32 total) march left/right, descending each reversal
 *   - Player cannon at bottom, moves left/right with joystick X
 *   - Single bullet: fire with button, must wait for it to leave screen before refiring
 *   - Alien bullets drop randomly from bottom-row aliens
 *   - Score: top rows worth more points
 *   - Game over: aliens reach the ground OR player bullet count hits 0 (3 lives)
 */

void  spaceinvaders_init(void);
void  spaceinvaders_input(int dx, int dy, bool btn);
bool  spaceinvaders_tick(uint32_t *score_out);
void  spaceinvaders_draw(void);

#ifdef __cplusplus
}
#endif