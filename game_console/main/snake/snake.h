#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SNAKE — 128x160 color version.
 *
 * GRID SIZING — recalculated for the larger color display:
 *   Old (128x64 mono):  CELL_SIZE=6, HUD_H=9  → grid 21x9   (189 cells)
 *   New (128x160 color): CELL_SIZE=8, HUD_H=16 → grid 16x18 (288 cells)
 *
 * CELL_SIZE went from 6 to 8 because the extra screen real-estate lets
 * the snake be chunkier and easier to see, while still fitting MORE
 * cells overall (288 vs 189) thanks to the much taller screen.
 *
 * HUD_H went from 9 to 16 to comfortably fit a larger, colored HUD font
 * with room to spare — the cramped 1px-margin HUD from the OLED version
 * isn't necessary anymore.
 */
#define CELL_SIZE   8
#define HUD_H       16
#define GRID_W      (128         / CELL_SIZE)   /* 16 columns */
#define GRID_H      ((160-HUD_H) / CELL_SIZE)   /* 18 rows    */

#define SNAKE_MAX_LEN       (GRID_W * GRID_H)   /* 288 max          */
#define SNAKE_START_LEN     3
#define SNAKE_START_X       (GRID_W / 2)
#define SNAKE_START_Y       (GRID_H / 2)

#define SPEED_INITIAL_MS    220
#define SPEED_MIN_MS        80
#define SPEED_STEP_MS       15
#define SPEED_FOOD_INTERVAL 3

#define FOOD_BLINK_MS       600

typedef struct {
    int8_t x;
    int8_t y;
} SnakeCell;

typedef enum {
    DIR_UP = 0,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT,
} Direction;

typedef enum {
    SNAKE_ALIVE,
    SNAKE_DEAD_WALL,
    SNAKE_DEAD_SELF,
    SNAKE_WIN,
} SnakeStatus;

typedef struct {
    SnakeCell   body[SNAKE_MAX_LEN];
    int         head_idx;
    int         length;

    Direction   dir;
    Direction   next_dir;

    SnakeCell   food;
    bool        food_visible;
    int64_t     food_blink_ms;

    uint32_t    score;
    uint32_t    food_eaten;
    int         step_ms;

    SnakeStatus status;
} SnakeGame;

void        snake_init(SnakeGame *g);
void        snake_input(SnakeGame *g, int dx, int dy);
SnakeStatus snake_tick(SnakeGame *g);
void        snake_draw(const SnakeGame *g);
uint32_t    snake_get_score(const SnakeGame *g);
int         snake_get_step_ms(const SnakeGame *g);

#ifdef __cplusplus
}
#endif