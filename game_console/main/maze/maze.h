#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MAZE / DUNGEON
 *
 * Mechanics distinct from all other games:
 *   - Procedurally generated maze using recursive backtracker (DFS)
 *   - Player navigates from top-left to bottom-right exit
 *   - Fog of war: only cells within 2 cells of player are revealed
 *   - Collectible coins scattered in dead-ends boost score
 *   - Timer counts up — faster completion = bonus score
 *   - Each completed maze generates a new harder one
 */

void  maze_init(void);
void  maze_input(int dx, int dy, bool btn);
bool  maze_tick(uint32_t *score_out);
void  maze_draw(void);

#ifdef __cplusplus
}
#endif