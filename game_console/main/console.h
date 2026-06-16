#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Each game exposes exactly these three symbols.
 * The console calls them via function pointers — adding a new game
 * is one struct literal in console.c, nothing else changes.
 */
typedef void     (*GameInitFn)(void);
typedef void     (*GameInputFn)(int dx, int dy, bool btn);
typedef bool     (*GameTickFn)(uint32_t *score_out);   /* returns false on game-over */
typedef void     (*GameDrawFn)(void);

typedef struct {
    const char *name;       /* shown in console menu, max ~10 chars */
    GameInitFn  init;
    GameInputFn input;
    GameTickFn  tick;
    GameDrawFn  draw;
} GameDesc;

/* Console lifecycle */
void console_init(void);

/*
 * Call every poll tick (20 ms).
 * dx/dy from joystick, btn = joystick click (active-low, already inverted).
 */
void console_input(int dx, int dy, bool btn);

/*
 * Call every loop iteration.
 * Handles timing internally; drives whichever game is active.
 */
void console_tick(void);

#ifdef __cplusplus
}
#endif