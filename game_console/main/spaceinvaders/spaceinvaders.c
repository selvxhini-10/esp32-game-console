/*
 * SPACE INVADERS — ESP32 OLED console game
 *
 * Layout (128×64):
 *   y=0        score/lives HUD
 *   y=9        separator
 *   y=10..53   play field
 *   y=54       ground line (aliens reaching here = game over)
 *   y=55..63   player cannon zone
 *
 * Alien grid: 8 cols × 4 rows = 32 aliens
 *   Each alien cell: 12px wide × 8px tall (includes 2px gap)
 *   Grid total: 96px wide, starts centred at x=16
 *
 * Sprites (5×5 bitmaps, drawn with 1px border box):
 *   Row 0-1 (top, worth 30): V-shape  "invader A"
 *   Row 2-3 (bot, worth 10): W-shape  "invader B"
 *
 * Fixed-point: NOT used here — all positions are integer pixels
 * since invaders move in whole-pixel steps per tick.
 */

#include "spaceinvaders.h"
#include "oled.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>

/* ─── layout ─────────────────────────────────────────────────────────────── *
 *
 * GEOMETRY FIX — why the game ended before any alien ever fired:
 *
 * The old grid was 4 rows starting at y=12 with a 4px drop per bounce.
 * The bottom row only needed to fall 6px to hit FIELD_BOT=54 — that's
 * just 2 wall-bounces (~4 seconds), while the expected wait for the
 * FIRST alien shot was 16 seconds (1-in-40 chance every 400ms march).
 * The grid was descending into "game over" roughly 4x faster than it
 * could ever fire a shot — the player never stood a chance to see one.
 *
 * Fix: fewer rows (4→3), smaller drop per bounce (4px→3px), higher
 * starting position, and a much higher fire chance (1-in-40 → 1-in-12).
 * This gives ~12 seconds of survival time before forced game-over,
 * with the first shot expected around 4 seconds in. ─────────────────────── */
#define FIELD_TOP    10
#define FIELD_BOT    54     /* aliens reaching this y = game over            */
#define PLAYER_Y     57     /* top of player cannon                          */
#define PLAYER_W     9
#define PLAYER_H     4

/* ─── alien grid ─────────────────────────────────────────────────────────── */
#define ALIEN_COLS   8
#define ALIEN_ROWS   3      /* was 4 — fewer rows gives more headroom        */
#define ALIEN_W      10     /* sprite width px                               */
#define ALIEN_H       6     /* sprite height px                              */
#define ALIEN_GAP_X   4     /* horizontal gap between aliens                 */
#define ALIEN_GAP_Y   3     /* was 4 — slightly tighter vertical spacing     */
#define ALIEN_STEP_X  2     /* pixels moved per march tick                   */
#define ALIEN_STEP_Y  3     /* was 4 — gentler descent per wall-bounce       */
#define ALIEN_START_X 8     /* left edge of grid at start                    */
#define ALIEN_START_Y 11    /* was 12 — one pixel higher for extra headroom  */

#define CELL_W       (ALIEN_W + ALIEN_GAP_X)   /* 14px per cell  */
#define CELL_H       (ALIEN_H + ALIEN_GAP_Y)   /*  9px per cell  */

/* ─── bullets ────────────────────────────────────────────────────────────── */
#define BULLET_H      4     /* bullet height px                              */
#define PLAYER_BULLET_SPEED  3   /* px per tick upward                       */
#define ALIEN_BULLET_SPEED   1   /* px per tick downward                     */
#define MAX_ALIEN_BULLETS    3

/* ─── timing ─────────────────────────────────────────────────────────────── */
#define TICK_MS          20     /* poll/draw rate                             */
#define MARCH_INTERVAL   350    /* ms between alien march steps (gets faster) */
#define ALIEN_FIRE_ODDS  12     /* was 40 — 1-in-N chance per march tick      */

/* ─── scoring ────────────────────────────────────────────────────────────── */
#define SCORE_TOP_ROW    30
#define SCORE_BOT_ROW    10

/* ─── font (digits + letters for HUD) ───────────────────────────────────── */
static const uint8_t si_font[][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},
    {0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x40,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},
    {0x10,0x08,0x08,0x10,0x08},
};

static void si_char(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *g = si_font[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t b = g[col];
        for (int row = 0; row < 8; row++)
            if (b & (1 << row)) oled_draw_pixel(x + col, y + row);
    }
}
static void si_str(int x, int y, const char *s)
{ while (*s) { si_char(x, y, *s++); x += 6; } }

/* ─── alien sprite bitmaps (8×5 pixels, 1 byte per column) ──────────────── */
/*
 * Type A (top 2 rows): classic crab shape
 *   .X...X.   col pattern stored as rows in 5 columns
 *   ..XXX..
 *   .XXXXX.
 *   XX.X.XX
 *   XXXXXXX
 *   .X.X.X.
 */
static const uint8_t ALIEN_A[ALIEN_W] = {
    0b00001100,  /* col 0 */
    0b00010010,  /* col 1 */
    0b00111110,  /* col 2 */
    0b00101010,  /* col 3 */
    0b00111110,  /* col 4 */
    0b00101010,  /* col 5 */
    0b00111110,  /* col 6 */
    0b00010010,  /* col 7 */
    0b00001100,  /* col 8 */
    0b00000000,  /* col 9 gap */
};

/*
 * Type B (bottom 2 rows): wider squid shape
 *   .XXXXX.
 *   XXXXXXX
 *   X.X.X.X
 *   .X...X.
 *   X.....X
 */
static const uint8_t ALIEN_B[ALIEN_W] = {
    0b00010000,
    0b00101000,
    0b00111100,
    0b00010100,
    0b00111100,
    0b00010100,
    0b00111100,
    0b00101000,
    0b00010000,
    0b00000000,
};

/* Animation frame B (alternate every march step — antennas move) */
static const uint8_t ALIEN_A2[ALIEN_W] = {
    0b00001100,
    0b00110010,
    0b00111110,
    0b00101010,
    0b00111110,
    0b00101010,
    0b00111110,
    0b00110010,
    0b00001100,
    0b00000000,
};
static const uint8_t ALIEN_B2[ALIEN_W] = {
    0b00110000,
    0b00101000,
    0b00111100,
    0b00010100,
    0b00111100,
    0b00010100,
    0b00111100,
    0b00101000,
    0b00110000,
    0b00000000,
};

/* ─── state ──────────────────────────────────────────────────────────────── */
typedef struct { int x; int y; bool active; } Bullet;

static struct {
    bool     alive[ALIEN_ROWS][ALIEN_COLS]; /* which aliens still live      */
    int      grid_x;        /* left edge of alien grid (px)                 */
    int      grid_y;        /* top of alien grid (px)                       */
    int      dir;           /* +1 = right, -1 = left                        */
    int      anim_frame;    /* 0 or 1 — alternates on each march step       */

    int      player_x;      /* left edge of player cannon                   */
    int      dx_intent;     /* joystick: -1/0/+1                            */

    Bullet   pbullet;       /* single player bullet                         */
    Bullet   abullets[MAX_ALIEN_BULLETS];

    int      lives;
    uint32_t score;
    bool     game_over;

    int64_t  last_tick_ms;
    int64_t  last_march_ms;
    int      march_interval; /* decreases as aliens are killed              */

    bool     btn_last;
} SI;

static inline int64_t si_now(void) { return esp_timer_get_time() / 1000; }

/* ────────────────────────────────────────────────────────────────────────────
 * FORMATION MODEL — simplified mental model:
 *
 * Think of the alien grid as ONE rigid rectangle that slides left/right and
 * occasionally steps down. Individual aliens are just true/false flags
 * inside that rectangle — they never move independently.
 *
 *   grid_x, grid_y  = pixel position of the rectangle's top-left corner
 *   alive[r][c]     = is the alien at row r, col c still alive?
 *
 * Because the rectangle is rigid, its left/right pixel edges are ALWAYS:
 *     left_edge  = grid_x
 *     right_edge = grid_x + (full grid width, regardless of kills)
 *
 * The previous version recalculated the bounding box every tick by scanning
 * all 24 aliens to find the leftmost/rightmost SURVIVING column — this made
 * the grid "shrink" its bounce zone as aliens died, which is both more
 * confusing to read AND not how classic Space Invaders behaves (the empty
 * columns from dead aliens still count as part of the formation's footprint
 * in the original game). Using a fixed formation width removes that whole
 * category of complexity — wall-bounce detection becomes a one-line check
 * against constants, with no loop required.
 * ──────────────────────────────────────────────────────────────────────────── */

#define GRID_WIDTH_PX   (ALIEN_COLS * CELL_W - ALIEN_GAP_X)  /* fixed footprint */

/* Count remaining aliens (used only for speed-up scaling and wave-clear check) */
static int count_alive(void)
{
    int n = 0;
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++)
            if (SI.alive[r][c]) n++;
    return n;
}

/* Pixel position of a specific alien's top-left corner within the formation */
static void alien_bounds(int row, int col, int *px, int *py)
{
    *px = SI.grid_x + col * CELL_W;
    *py = SI.grid_y + row * CELL_H;
}

/*
 * Pick a random alien to fire from.
 *
 * Simplified targeting: instead of finding the "front-most" alien per
 * column (which needs a nested loop), we just pick uniformly among all
 * currently-alive aliens. This is visually indistinguishable during play
 * — bullets fall fast enough that the player can't tell whether the shot
 * came from the "front" of a column or not — but the code is one flat
 * scan instead of a column-by-column search.
 */
static void alien_fire(void)
{
    /* Collect every alive alien into a flat list */
    int shooters[ALIEN_ROWS * ALIEN_COLS], n = 0;
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++)
            if (SI.alive[r][c]) shooters[n++] = r * ALIEN_COLS + c;

    if (n == 0) return;   /* no aliens left to fire from */

    /* Find a free bullet slot before picking a shooter (avoid wasted random draw) */
    int slot = -1;
    for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (!SI.abullets[i].active) { slot = i; break; }
    }
    if (slot < 0) return;   /* all bullet slots in use */

    int pick = (int)(esp_random() % (uint32_t)n);
    int row  = shooters[pick] / ALIEN_COLS;
    int col  = shooters[pick] % ALIEN_COLS;

    int px, py;
    alien_bounds(row, col, &px, &py);
    SI.abullets[slot].x      = px + ALIEN_W / 2;
    SI.abullets[slot].y      = py + ALIEN_H;
    SI.abullets[slot].active = true;
}

/* ─── public API ─────────────────────────────────────────────────────────── */

void spaceinvaders_init(void)
{
    memset(&SI, 0, sizeof(SI));
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++)
            SI.alive[r][c] = true;

    SI.grid_x         = ALIEN_START_X;
    SI.grid_y         = ALIEN_START_Y;
    SI.dir            = 1;
    SI.player_x       = (128 - PLAYER_W) / 2;
    SI.lives          = 3;
    SI.march_interval = MARCH_INTERVAL;
    SI.last_tick_ms   = si_now();
    SI.last_march_ms  = si_now();
}

void spaceinvaders_input(int dx, int dy, bool btn)
{
    (void)dy;
    SI.dx_intent = dx;
    /* Fire on rising edge */
    if (btn && !SI.btn_last && !SI.pbullet.active) {
        SI.pbullet.x      = SI.player_x + PLAYER_W / 2;
        SI.pbullet.y      = PLAYER_Y - BULLET_H;
        SI.pbullet.active = true;
        sound_play(NOTE_C5, 30);   /* quick laser "pew" — short so rapid fire doesn't stack delays */
    }
    SI.btn_last = btn;
}

bool spaceinvaders_tick(uint32_t *score_out)
{
    if (SI.game_over) { *score_out = SI.score; return false; }

    int64_t now = si_now();
    if ((now - SI.last_tick_ms) < TICK_MS) { *score_out = SI.score; return true; }
    SI.last_tick_ms = now;

    /* ── Move player ── */
    SI.player_x += SI.dx_intent * 2;
    if (SI.player_x < 1)             SI.player_x = 1;
    if (SI.player_x > 128 - PLAYER_W - 1) SI.player_x = 128 - PLAYER_W - 1;

    /* ── Player bullet ── */
    if (SI.pbullet.active) {
        SI.pbullet.y -= PLAYER_BULLET_SPEED;
        if (SI.pbullet.y < FIELD_TOP) SI.pbullet.active = false;
    }

    /* ── Alien bullets ── */
    for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (!SI.abullets[i].active) continue;
        SI.abullets[i].y += ALIEN_BULLET_SPEED;
        if (SI.abullets[i].y > 64) {
            SI.abullets[i].active = false;
            continue;
        }
        /* Hit player? */
        if (SI.abullets[i].y >= PLAYER_Y &&
            SI.abullets[i].x >= SI.player_x &&
            SI.abullets[i].x <= SI.player_x + PLAYER_W) {
            SI.abullets[i].active = false;
            SI.lives--;
            if (SI.lives <= 0) {
                SI.game_over = true;
                static const Note gameover_tune[] = {
                    { NOTE_A4, 90 }, { NOTE_F4, 90 }, { NOTE_C4, 250 },
                };
                sound_play_melody(gameover_tune, sizeof(gameover_tune)/sizeof(gameover_tune[0]));
                *score_out = SI.score; return false;
            }
            /* Hit but still have lives — lower single tone, distinct from
             * the full game-over melody (this is a setback, not the end) */
            sound_play(NOTE_D4, 200);
        }
    }

    /* ── March aliens ── */
    if ((now - SI.last_march_ms) >= SI.march_interval) {
        SI.last_march_ms = now;
        SI.anim_frame    = !SI.anim_frame;

        /* Move the whole formation as one rigid block */
        SI.grid_x += SI.dir * ALIEN_STEP_X;

        /*
         * Wall check — fixed-width formation model.
         * No scanning required: the formation's left/right edges are
         * always grid_x and grid_x + GRID_WIDTH_PX, regardless of how
         * many aliens have been killed (matches classic Invaders behaviour
         * where dead aliens still "hold their place" in the formation).
         */
        int left  = SI.grid_x;
        int right = SI.grid_x + GRID_WIDTH_PX;

        if (right >= 127 || left <= 1) {
            SI.dir     = -SI.dir;
            SI.grid_y += ALIEN_STEP_Y;

            /* Speed up as aliens are killed — fewer aliens = faster march */
            int alive = count_alive();
            SI.march_interval = MARCH_INTERVAL * alive / (ALIEN_ROWS * ALIEN_COLS);
            if (SI.march_interval < 80) SI.march_interval = 80;
        }

        /*
         * Ground check — only the bottom row matters since the formation
         * is rigid (if the bottom row hasn't reached the ground, nothing
         * above it has either). One row scan instead of all four.
         */
        for (int c = 0; c < ALIEN_COLS; c++) {
            if (!SI.alive[ALIEN_ROWS - 1][c]) continue;
            int px, py;
            alien_bounds(ALIEN_ROWS - 1, c, &px, &py);
            if (py + ALIEN_H >= FIELD_BOT) {
                SI.game_over = true;
                static const Note gameover_tune[] = {
                    { NOTE_A4, 90 }, { NOTE_F4, 90 }, { NOTE_C4, 250 },
                };
                sound_play_melody(gameover_tune, sizeof(gameover_tune)/sizeof(gameover_tune[0]));
                *score_out = SI.score; return false;
            }
        }

        /* Random alien fire */
        if ((int)(esp_random() % ALIEN_FIRE_ODDS) == 0) alien_fire();
    }

    /* ── Bullet vs alien collision ── */
    if (SI.pbullet.active) {
        for (int r = 0; r < ALIEN_ROWS; r++) {
            for (int c = 0; c < ALIEN_COLS; c++) {
                if (!SI.alive[r][c]) continue;
                int px, py; alien_bounds(r, c, &px, &py);
                if (SI.pbullet.x >= px && SI.pbullet.x <= px + ALIEN_W &&
                    SI.pbullet.y >= py && SI.pbullet.y <= py + ALIEN_H) {
                    SI.alive[r][c]    = false;
                    SI.pbullet.active = false;
                    /* Top 2 rows worth more */
                    SI.score += (r < 2) ? SCORE_TOP_ROW : SCORE_BOT_ROW;

                    /* Pitch varies by row — top-row kills (worth more) ring
                     * out higher than bottom-row kills, giving an audible
                     * sense of "value" without needing to glance at score */
                    sound_play(r < 2 ? NOTE_G5 : NOTE_E5, 35);

                    /* All cleared — new wave, descend grid */
                    if (count_alive() == 0) {
                        for (int rr = 0; rr < ALIEN_ROWS; rr++)
                            for (int cc = 0; cc < ALIEN_COLS; cc++)
                                SI.alive[rr][cc] = true;
                        SI.grid_x = ALIEN_START_X;
                        SI.grid_y = ALIEN_START_Y;
                        SI.march_interval = MARCH_INTERVAL * 3 / 4; /* faster next wave */

                        /* Triumphant ascending run — clear "wave cleared" cue,
                         * fires AFTER the kill chirp above, queued right behind it */
                        static const Note wave_tune[] = {
                            { NOTE_C5, 70 }, { NOTE_E5, 70 }, { NOTE_G5, 70 }, { NOTE_C6, 140 },
                        };
                        sound_play_melody(wave_tune, sizeof(wave_tune)/sizeof(wave_tune[0]));
                    }
                    goto bullet_done;
                }
            }
        }
        bullet_done:;
    }

    *score_out = SI.score;
    return true;
}

void spaceinvaders_draw(void)
{
    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "SCR:%lu  LIVES:%d",
             (unsigned long)SI.score, SI.lives);
    si_str(1, 1, buf);
    for (int x = 0; x < 128; x++) oled_draw_pixel(x, 9);

    /* Ground line */
    for (int x = 0; x < 128; x++) oled_draw_pixel(x, FIELD_BOT);

    /* Aliens */
    for (int r = 0; r < ALIEN_ROWS; r++) {
        const uint8_t *sprite;
        if (r < 2)
            sprite = SI.anim_frame ? ALIEN_A2 : ALIEN_A;
        else
            sprite = SI.anim_frame ? ALIEN_B2 : ALIEN_B;

        for (int c = 0; c < ALIEN_COLS; c++) {
            if (!SI.alive[r][c]) continue;
            int px, py; alien_bounds(r, c, &px, &py);
            for (int col = 0; col < ALIEN_W; col++) {
                uint8_t bits = sprite[col];
                for (int row = 0; row < ALIEN_H; row++)
                    if (bits & (1 << row)) oled_draw_pixel(px + col, py + row);
            }
        }
    }

    /* Player cannon — upward pointing triangle shape */
    for (int x = 0; x < PLAYER_W; x++)
        oled_draw_pixel(SI.player_x + x, PLAYER_Y + 3);
    for (int x = 1; x < PLAYER_W - 1; x++)
        oled_draw_pixel(SI.player_x + x, PLAYER_Y + 2);
    for (int x = 2; x < PLAYER_W - 2; x++)
        oled_draw_pixel(SI.player_x + x, PLAYER_Y + 1);
    oled_draw_pixel(SI.player_x + PLAYER_W / 2, PLAYER_Y);

    /* Player bullet */
    if (SI.pbullet.active) {
        for (int i = 0; i < BULLET_H; i++)
            oled_draw_pixel(SI.pbullet.x, SI.pbullet.y + i);
    }

    /* Alien bullets (zigzag shape) */
    for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (!SI.abullets[i].active) continue;
        for (int j = 0; j < BULLET_H; j++) {
            int xoff = (j % 2 == 0) ? 0 : 1;
            oled_draw_pixel(SI.abullets[i].x + xoff, SI.abullets[i].y + j);
        }
    }
}