/*
 * SPLASH_FONT — stylized 2x-scale word rendering for the boot splash.
 *
 * See splash_font.h for the full root-cause explanation of the slant bug.
 * Summary: SPLASH_STYLE_SLANTED is now an explicit, named switch case
 * instead of an unreachable-by-design 'default' fallback that any
 * undefined/mismatched style value would silently land on.
 */

#include "splash_font.h"
#include "font.h"
#include "tft.h"
#include <string.h>

/*
 * CHAR_ADVANCE_2X — pixel width consumed per character at 2x scale.
 *
 * font.c's font_draw_char_2x() draws a 5-column glyph at 2x scale, i.e.
 * 10px wide, and font_draw_str_2x() advances by 11px per character (10px
 * glyph + 1px gap). splash_font.c's bold/outline/glow renderers were
 * advancing by 12px per character instead — a 1px-per-character drift
 * that, compounded across a 7-letter word like "CONSOLE", added up to
 * several pixels of unintended extra width versus what splash.c's
 * centring math (X_ESP32/X_GAME/X_CONSOLE, calculated using 11px/char)
 * assumed. Standardising on 11px everywhere removes that drift.
 */
#define CHAR_ADVANCE_2X  11

/* ─────────────────────────────────────────────
   BOLD
   ───────────────────────────────────────────── */
void splash_font_draw_2x_bold(int x, int y, const char *s, uint16_t color)
{
    while (*s) {
        char c = *s++;

        font_draw_char_2x(x,     y, c, color);
        font_draw_char_2x(x + 1, y, c, color);
        font_draw_char_2x(x, y + 1, c, color);

        x += CHAR_ADVANCE_2X;
    }
}

/* ─────────────────────────────────────────────
   OUTLINE
   ───────────────────────────────────────────── */
void splash_font_draw_2x_outline(int x, int y, const char *s, uint16_t color)
{
    const int o = 1;

    while (*s) {
        char c = *s++;

        font_draw_char_2x(x - o, y, c, color);
        font_draw_char_2x(x + o, y, c, color);
        font_draw_char_2x(x, y - o, c, color);
        font_draw_char_2x(x, y + o, c, color);

        font_draw_char_2x(x, y, c, color);

        x += CHAR_ADVANCE_2X;
    }
}

/* ─────────────────────────────────────────────
   GLOW (stable blur)
   ───────────────────────────────────────────── */
static inline uint16_t dim(uint16_t c)
{
    return (c >> 1) & 0x7BEF;
}

void splash_font_draw_2x_glow(int x, int y, const char *s, uint16_t color)
{
    const int r = 1;

    for (int dx = -r; dx <= r; dx++) {
        for (int dy = -r; dy <= r; dy++) {
            if (dx == 0 && dy == 0) continue;
            font_draw_str_2x(x + dx, y + dy, s, dim(color));
        }
    }

    splash_font_draw_2x_bold(x, y, s, color);
}

/* ─────────────────────────────────────────────
   SLANTED — now an EXPLICIT, deliberately-chosen style.
   Previously this exact rendering logic lived only in the switch's
   'default' case, meaning ANY style value that failed to match BOLD/
   OUTLINE/GLOW silently rendered slanted. Promoted to its own named
   function + its own named switch case (see splash_font_draw_word
   below) so it can only ever render when SPLASH_STYLE_SLANTED is
   explicitly requested — never as an accidental fallback.
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
   COLUMN REVEAL — left-to-right character reveal
   ───────────────────────────────────────────── */
void splash_font_draw_2x_column_reveal(
    int x, int y,
    const char *s,
    uint16_t color,
    int reveal_cols)
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
    /*
     * No 'default' case. Every SplashStyle enum value declared in
     * splash_font.h has an explicit, matching case here. If a future
     * style is added to the enum but a case is forgotten here, most
     * compilers will warn ("enumeration value not handled in switch")
     * with -Wswitch, which is the correct failure mode — a compile-time
     * warning instead of a silent runtime fallback that took a careful
     * trace to diagnose this time.
     */
    switch (style) {
        case SPLASH_STYLE_BOLD:
            splash_font_draw_2x_bold(x, y, s, color);
            break;

        case SPLASH_STYLE_OUTLINE:
            splash_font_draw_2x_outline(x, y, s, color);
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
    /* len full-advance characters, minus the trailing 1px gap that the
     * last character doesn't need — matches font_draw_str_2x_centred's
     * own width math in font.c for consistency. */
    return len * CHAR_ADVANCE_2X - 1;
}