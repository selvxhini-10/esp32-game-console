#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SPLASH_FONT — stylized 2x-scale word rendering for the boot splash.
 *
 * ROOT CAUSE OF THE SLANT BUG:
 *
 * splash_font_draw_word()'s switch statement matched against
 * SPLASH_STYLE_BOLD/OUTLINE/GLOW, with a 'default' case that rendered
 * a deliberately SLANTED fallback (each character offset further down
 * as x increases — see the old default branch:
 *     font_draw_char_2x(x + offset, y + (offset >> 4), *s++, color);
 * ). That fallback exists, by design, for any style value that doesn't
 * match one of the three named cases.
 *
 * The actual bug was that this header (splash_font.h) was never
 * provided alongside splash_font.c and splash.c — meaning the enum
 * values SPLASH_STYLE_BOLD/OUTLINE/GLOW/SLANTED were UNDEFINED from
 * this file's perspective. Without a guaranteed, explicit enum
 * declaration, there's no way to verify the caller (splash.c) and the
 * switch (splash_font.c) agree on which integer means which style —
 * any mismatch silently falls through to the slanted default, which
 * is exactly the "slanted, doesn't fit the viewport" symptom reported
 * (slanted text drifts down-and-right per character until it runs past
 * the screen edge, where tft_draw_pixel's bounds check silently drops
 * the off-screen pixels — looking like clipped/cut-off text).
 *
 * FIX: explicit integer values on every enum member, so there is no
 * ambiguity possible between this header and splash_font.c's switch,
 * regardless of include order or any stray definition elsewhere.
 */
typedef enum {
    SPLASH_STYLE_BOLD    = 0,
    SPLASH_STYLE_OUTLINE = 1,
    SPLASH_STYLE_GLOW    = 2,
    SPLASH_STYLE_SLANTED = 3,   /* kept as an explicit, intentional style —
                                   no longer reachable as an accidental
                                   fallthrough default, see splash_font.c */
} SplashStyle;

/* Individual style renderers, exposed in case any caller wants one directly */
void splash_font_draw_2x_bold(int x, int y, const char *s, uint16_t color);
void splash_font_draw_2x_outline(int x, int y, const char *s, uint16_t color);
void splash_font_draw_2x_glow(int x, int y, const char *s, uint16_t color);
void splash_font_draw_2x_slanted(int x, int y, const char *s, uint16_t color);

void splash_font_draw_2x_column_reveal(int x, int y, const char *s,
                                        uint16_t color, int reveal_cols);

/* Style dispatch — style must be a SplashStyle value */
void splash_font_draw_word(int x, int y, const char *s, uint16_t color,
                            SplashStyle style);

/*
 * Width helper — returns the pixel width of a word at 2x scale, using the
 * SAME per-character advance the renderers actually use (12px), so
 * callers computing centred X positions get a value that matches what
 * will actually be drawn. See splash.c's previous X_ESP32/X_GAME/
 * X_CONSOLE macros, which used 11px/char (font.c's plain advance) while
 * splash_font.c's renderers all advance 12px/char — a mismatch that
 * caused a few pixels of centering drift on every word.
 */
int splash_font_word_width(const char *s);

#ifdef __cplusplus
}
#endif