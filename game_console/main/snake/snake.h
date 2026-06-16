#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================
   GRID CONFIGURATION
   ========================= */

/*
 * CELL_SIZE 6 — was 4. Larger cells mean:
 *   - snake body is 6×6px (vs 4×4), visually chunky and clear
 *   - apple fills ~7px centred in 6px cell — easily spotted
 *   - hitbox is the full 6×6 cell so no pixel-perfect aim needed
 *
 * HUD_H — top 9 pixels reserved for score strip.
 * The play area starts at y=HUD_H, giving a 128×(64-HUD_H) canvas.
 *
 * Grid: 128/6 = 21 columns, (64-9)/6 = 9 rows  (189 cells total)
 */
#define CELL_SIZE   6
#define HUD_H       9                           /* score strip height in px  */
#define GRID_W      (128        / CELL_SIZE)    /* 21 columns                */
#define GRID_H      ((64-HUD_H) / CELL_SIZE)   /*  9 rows                   */

/* =========================
   GAME TUNING
   ========================= */

#define SNAKE_MAX_LEN       (GRID_W * GRID_H)   /* 189 max                   */
#define SNAKE_START_LEN     3
#define SNAKE_START_X       (GRID_W / 2)
#define SNAKE_START_Y       (GRID_H / 2)

#define SPEED_INITIAL_MS    220
#define SPEED_MIN_MS        80
#define SPEED_STEP_MS       15
#define SPEED_FOOD_INTERVAL 3                   /* speed up every 3 apples   */

#define FOOD_BLINK_MS       600                 /* apple blink period (ms)   */

/* =========================
   TYPES
   ========================= */

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
    /* Body ring buffer */
    SnakeCell   body[SNAKE_MAX_LEN];
    int         head_idx;
    int         length;

    /* Movement */
    Direction   dir;
    Direction   next_dir;

    /* Food */
    SnakeCell   food;
    bool        food_visible;
    int64_t     food_blink_ms;

    /* Score / speed */
    uint32_t    score;
    uint32_t    food_eaten;
    int         step_ms;

    /* State */
    SnakeStatus status;
} SnakeGame;

/* =========================
   PUBLIC API
   ========================= */

void        snake_init(SnakeGame *g);
void        snake_input(SnakeGame *g, int dx, int dy);
SnakeStatus snake_tick(SnakeGame *g);
void        snake_draw(const SnakeGame *g);
uint32_t    snake_get_score(const SnakeGame *g);
int         snake_get_step_ms(const SnakeGame *g);

#ifdef __cplusplus
}
#endif