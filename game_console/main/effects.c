/*
 * EFFECTS — twinkling stars and retro heart sprites.
 *
 * STAR TWINKLE ALGORITHM — deliberately simple, no trig:
 * Each star has a fixed (x,y) position and a "phase" counter that
 * increments every frame. The star's brightness/visibility cycles
 * through 4 states based on (phase % cycle_len), going through:
 *   dim -> bright -> dim -> OFF -> repeat
 * This gives an irregular, organic-looking twinkle (since each star's
 * phase offset and cycle length differ) using only integer addition and
 * modulo per star per frame — no floating point, no sin/cos lookup
 * tables, trivial CPU cost even with 20 stars redrawn every frame.
 */

#include "effects.h"
#include "tft.h"
#include "palette.h"
#include "esp_random.h"
#include <string.h>

typedef struct {
    int16_t x, y;
    uint8_t phase;       /* current position in this star's twinkle cycle */
    uint8_t cycle_len;    /* total frames in this star's twinkle cycle     */
} Star;

static Star s_stars[EFFECTS_MAX_STARS];
static int  s_star_count = 0;

void effects_stars_init(int screen_w, int screen_h)
{
    s_star_count = EFFECTS_MAX_STARS;
    for (int i = 0; i < s_star_count; i++) {
        s_stars[i].x         = (int16_t)(esp_random() % (uint32_t)screen_w);
        s_stars[i].y         = (int16_t)(esp_random() % (uint32_t)screen_h);
        s_stars[i].phase     = (uint8_t)(esp_random() % 60);
        /* Cycle length varies 40-90 frames so stars don't all twinkle in
         * lockstep — this randomised desync is what makes it look organic
         * rather than a single blinking pattern repeated everywhere. */
        s_stars[i].cycle_len = (uint8_t)(40 + (esp_random() % 50));
    }
}

void effects_stars_draw(void)
{
    for (int i = 0; i < s_star_count; i++) {
        Star *s = &s_stars[i];
        s->phase = (uint8_t)((s->phase + 1) % s->cycle_len);

        /* Map phase into one of 4 visual states across the cycle:
         *   first quarter  -> dim (PAL_OUTLINE)
         *   second quarter -> bright (PAL_BLUE_BRIGHT)
         *   third quarter  -> dim again
         *   fourth quarter -> off entirely (skip draw)
         * This produces a fade-in/fade-out-feeling twinkle from only 2
         * actual color states plus an "off" gap, no real interpolation. */
        int quarter = (s->phase * 4) / s->cycle_len;

        switch (quarter) {
            case 0:
            case 2:
                tft_draw_pixel(s->x, s->y, PAL_OUTLINE);
                break;
            case 1:
                tft_draw_pixel(s->x, s->y, PAL_BLUE_BRIGHT);
                break;
            case 3:
            default:
                /* off this frame — star briefly disappears, matches a
                 * real twinkle's occasional near-invisible moment */
                break;
        }
    }
}

/*
 * Retro heart sprite — 7x6 pixel-art heart, drawn with tft_draw_pixel
 * calls following a small fixed coordinate list rather than a bitmap
 * array, since the shape is simple enough that hardcoding the on-pixels
 * directly is both clearer to read and just as compact as a packed
 * bitmap would be at this size.
 *
 *   .XX.XX.
 *   XXXXXXX
 *   XXXXXXX
 *   .XXXXX.
 *   ..XXX..
 *   ...X...
 *
 * filled=false draws only the OUTLINE pixels (the shape's silhouette
 * edge) in a dim color, giving a clearly "empty/used" heart look for
 * lives already lost, vs filled=true drawing the full solid shape.
 */
void effects_draw_heart(int x, int y, bool filled, uint16_t color)
{
    static const uint8_t HEART_ROWS[6] = {
        0b0110110,
        0b1111111,
        0b1111111,
        0b0111110,
        0b0011100,
        0b0001000,
    };
    /* Outline-only pixel set — just the silhouette edge, used when
     * filled=false so an "empty" heart still reads as heart-shaped
     * rather than a vague dim blob. */
    static const uint8_t HEART_OUTLINE_ROWS[6] = {
        0b0110110,
        0b1000001,
        0b1000001,
        0b0100010,
        0b0011100,
        0b0001000,
    };

    const uint8_t *rows = filled ? HEART_ROWS : HEART_OUTLINE_ROWS;

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            if (rows[row] & (1 << (6 - col))) {
                tft_draw_pixel(x + col, y + row, color);
            }
        }
    }
}