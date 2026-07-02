/*
 * SPLASH_FONT — stylized 2x-scale word rendering for the boot splash.
 *
 * READABILITY FIX — why bold was removed as the primary logo style:
 *
 * The previous BOLD renderer stacked three draw calls at (+0,+0),
 * (+1,+0), and (+0,+1) offsets. At 2x scale on a 160x128 display, a
 * single pixel column in the source glyph is already 2px wide. Adding
 * a +1 horizontal offset smears each column into a 3px-wide blob —
 * strokes that should be 2px wide become 3px, thin gaps between strokes
 * collapse entirely, and small characters ('E','S','3') lose interior
 * detail completely. The result is a blurry blob, not a crisp logo.
 *
 * OUTLINE is the correct weight for this display: one-pixel border in a
 * dim color, clean glyph on top. Achieves visual heaviness without
 * touching interior stroke geometry — thin gaps stay thin.
 *
 * GLOW: 3x3 neighborhood halo at half-brightness, then clean core drawn
 * with font_draw_str_2x directly (old version called bold() for the core,
 * compounding the blur — fixed here).
 */

#include "splash_font.h"
#include "font.h"
#include "tft.h"
#include "palette.h"
#include <string.h>

#define CHAR_ADVANCE_2X  11   /* must match font_draw_str_2x's advance */

/* ─────────────────────────────────────────────
   INTERNAL: two-pass outline (border then core)
   ───────────────────────────────────────────── */
static void draw_outline(int x, int y, const char *s, uint16_t color)
{
    /* Pass 1: cardinal-direction border in dim palette blue */
    int cx = x;
    for (const char *p = s; *p; p++) {
        font_draw_char_2x(cx - 1, y,     *p, PAL_OUTLINE);
        font_draw_char_2x(cx + 1, y,     *p, PAL_OUTLINE);
        font_draw_char_2x(cx,     y - 1, *p, PAL_OUTLINE);
        font_draw_char_2x(cx,     y + 1, *p, PAL_OUTLINE);
        cx += CHAR_ADVANCE_2X;
    }
    /* Pass 2: clean core glyph on top */
    cx = x;
    for (const char *p = s; *p; p++) {
        font_draw_char_2x(cx, y, *p, color);
        cx += CHAR_ADVANCE_2X;
    }
}

/* ─────────────────────────────────────────────
   BOLD — kept for API compat. No longer stacks pixel offsets.
   At 2x scale a plain draw is already heavier than 1x; the old
   +1x/+1y stacking collapsed interior strokes into blobs.
   ───────────────────────────────────────────── */
void splash_font_draw_2x_bold(int x, int y, const char *s, uint16_t color)
{
    int cx = x;
    for (const char *p = s; *p; p++) {
        font_draw_char_2x(cx, y, *p, color);
        cx += CHAR_ADVANCE_2X;
    }
}

/* ─────────────────────────────────────────────
   OUTLINE — primary logo weight, delegates to internal helper.
   ───────────────────────────────────────────── */
void splash_font_draw_2x_outline(int x, int y, const char *s, uint16_t color)
{
    draw_outline(x, y, s, color);
}

/* ─────────────────────────────────────────────
   GLOW — dim 3x3 halo, then clean core (no bold stacking).
   ───────────────────────────────────────────── */
static inline uint16_t dim_color(uint16_t c)
{
    return (c >> 1) & 0x7BEF;
}

void splash_font_draw_2x_glow(int x, int y, const char *s, uint16_t color)
{
    uint16_t glow = dim_color(color);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            font_draw_str_2x(x + dx, y + dy, s, glow);
        }
    font_draw_str_2x(x, y, s, color);   /* clean core — no bold stacking */
}

/* ─────────────────────────────────────────────
   SLANTED — explicit style, never an accidental fallback.
   ───────────────────────────────────────────── */
void splash_font_draw_2x_slanted(int x, int y, const char *s, uint16_t color)
{
    int offset = 0;
    while (*s) {
        font_draw_char_2x(x + offset, y + (offset >> 4), *s++, color);
        offset += CHAR_ADVANCE_2X;
    }
}

/* ─────────────────────────────────────────────
   COLUMN REVEAL — typewriter left-to-right character reveal.
   ───────────────────────────────────────────── */
void splash_font_draw_2x_column_reveal(
    int x, int y, const char *s, uint16_t color, int reveal_cols)
{
    int cx = x;
    for (const char *p = s; *p; p++) {
        if ((p - s) >= reveal_cols) break;
        font_draw_char_2x(cx, y, *p, color);
        cx += CHAR_ADVANCE_2X;
    }
}

/* ─────────────────────────────────────────────
   STYLE DISPATCH
   ───────────────────────────────────────────── */
void splash_font_draw_word(int x, int y, const char *s, uint16_t color,
                            SplashStyle style)
{
    switch (style) {
        case SPLASH_STYLE_BOLD:
            splash_font_draw_2x_bold(x, y, s, color);
            break;
        case SPLASH_STYLE_OUTLINE:
            draw_outline(x, y, s, color);
            break;
        case SPLASH_STYLE_GLOW:
            splash_font_draw_2x_glow(x, y, s, color);
            break;
        case SPLASH_STYLE_SLANTED:
            splash_font_draw_2x_slanted(x, y, s, color);
            break;
    }
}

/* ─────────────────────────────────────────────
   WIDTH HELPER
   ───────────────────────────────────────────── */
int splash_font_word_width(const char *s)
{
    int len = (int)strlen(s);
    if (len == 0) return 0;
    return len * CHAR_ADVANCE_2X - 1;
}