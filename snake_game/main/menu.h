#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================
   MENU STATE MACHINE
   ========================= */

typedef enum {
    MENU_STATE_MAIN,        /* Top-level: "DINO RUN" title + Start / Highscore */
    MENU_STATE_HIGHSCORE,   /* Highscore screen */
    MENU_STATE_COUNTDOWN,   /* 3-2-1 before gameplay starts */
    MENU_STATE_GAME,        /* Active gameplay (hand-off to game logic) */
    MENU_STATE_GAME_OVER,   /* "GAME OVER" + score + replay prompt */
} MenuState;

typedef enum {
    MENU_ITEM_START = 0,
    MENU_ITEM_HIGHSCORE,
    MENU_ITEM_COUNT         /* keep last */
} MainMenuItem;

/* =========================
   RECT COLLISION HELPERS
   ========================= */

typedef struct {
    int x, y;   /* top-left origin */
    int w, h;   /* width, height   */
} Rect;

/* Returns true if r is fully within display bounds (inclusive) */
bool rect_in_bounds(const Rect *r);

/* Clamps rect so it cannot leave the display area */
void rect_clamp_to_bounds(Rect *r);

/* Returns true if two rects overlap (AABB) */
bool rect_intersects(const Rect *a, const Rect *b);

/* =========================
   MENU API
   ========================= */

/* Call once at startup */
void menu_init(void);

/* Feed joystick input each frame.
   dy  : -1 = up, +1 = down, 0 = neutral   (Y axis)
   btn : true on joystick-click rising edge */
void menu_input(int dy, bool btn);

/* Draw the current menu screen into the OLED framebuffer,
   then call oled_update(). Call at your target frame rate. */
void menu_draw(void);

/* Returns the current state (game logic polls for MENU_STATE_GAME) */
MenuState menu_get_state(void);

/* Call from game logic when the player dies */
void menu_notify_game_over(uint32_t final_score);

/* Returns the all-time high score (persisted in RAM; add NVS later) */
uint32_t menu_get_highscore(void);

#ifdef __cplusplus
}
#endif