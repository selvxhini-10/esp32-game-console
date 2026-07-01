#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EFFECTS — lightweight ambient visual effects (twinkling stars, retro
 * heart sprites for lives) shared across menus and games.
 *
 * Designed deliberately cheap: a small fixed-size star array (no heap
 * allocation), simple per-frame counters instead of trig functions, and
 * draw calls that are just a handful of tft_draw_pixel/fill_rect calls —
 * comfortably within budget on a plain ESP32 even called every frame
 * from a menu screen that's otherwise mostly static.
 */

#define EFFECTS_MAX_STARS  20

/* Call once at boot (or whenever entering a screen that wants a fresh
 * starfield layout) — randomises star positions/phases. */
void effects_stars_init(int screen_w, int screen_h);

/* Call once per frame from any screen that wants a twinkling background.
 * Advances each star's twinkle phase and draws it. Cheap enough to call
 * unconditionally from menu draw functions. */
void effects_stars_draw(void);

/*
 * Retro heart sprite for lives/health display — drawn as a small pixel-
 * art heart rather than a plain filled square, matching the reference
 * image's heart icons. filled=true draws a solid/bright heart (life
 * remaining), filled=false draws a dim outline-only heart (life lost),
 * so a life bar can show e.g. 2 filled + 1 empty heart at a glance.
 */
void effects_draw_heart(int x, int y, bool filled, uint16_t color);

#ifdef __cplusplus
}
#endif