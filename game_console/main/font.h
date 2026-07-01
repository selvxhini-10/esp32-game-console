#pragma once

#include <stdint.h>
#include "tft.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FONT — shared 5x7 bitmap font, now color-aware.
 *
 * This consolidates 8 separate copies of the same font table that had
 * accumulated across snake.c, pong.c, breakout.c, flappy.c, maze.c,
 * spaceinvaders.c (via console.c's con_font), console.c, splash.c, and
 * menu.c — each file had pasted its own copy with a different static
 * array name (font5x7, s_font, con_font, sp_font, fl_font, mz_font,
 * brk_font...) but byte-for-byte identical bitmap data.
 *
 * Now that every draw call needs a color parameter for the TFT migration,
 * this was the natural point to merge them into one module instead of
 * making the same signature change in 8 places.
 */

void font_draw_char(int x, int y, char c, uint16_t color);

/* Returns the pixel width consumed (always len*6 for this fixed-width font) */
int font_draw_str(int x, int y, const char *s, uint16_t color);

void font_draw_str_centred(int y, const char *s, uint16_t color, int screen_width);

/* 2x scaled variants — useful for titles/countdowns on the larger screen */
void font_draw_char_2x(int x, int y, char c, uint16_t color);
void font_draw_str_2x(int x, int y, const char *s, uint16_t color);
void font_draw_str_2x_centred(int y, const char *s, uint16_t color, int screen_width);

/* Top-down reveal variant used by the splash screen animation */
void font_draw_str_2x_reveal(int x, int y, const char *s, uint16_t color, int reveal_px);

#ifdef __cplusplus
}
#endif