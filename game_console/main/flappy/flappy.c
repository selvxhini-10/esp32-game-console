/*
 * FLAPPY — gravity-based side-scroller for the ESP32 OLED console.
 *
 * Physics: fixed-point Q4 (values are ×16 of real pixels).
 * Display: 128×64 px, 1-bit.
 *
 * Layout:
 *   y=0..55   play field
 *   y=56      ground line
 *   y=57..63  HUD strip (score)
 *
 * Pipes:
 *   Two pipe slots scroll left. Each slot holds an (x, gap_y) pair.
 *   gap_y is the top of the opening; opening height is GAP_H pixels.
 *   When a pipe exits the left edge it is recycled to the right
 *   with a new random gap_y.
 *
 * Collision:
 *   Bird is a 5×5 pixel box. Collision is axis-aligned rectangle vs
 *   pipe rectangles and ground.
 */

#include "flappy.h"
#include "oled.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>

/* ─── layout ─────────────────────────────────────────────────────────────── */
#define FIELD_H     56          /* play area height                */
#define GROUND_Y    56          /* y of ground line                */
#define HUD_Y       58          /* score text y                    */

/* ─── bird ───────────────────────────────────────────────────────────────── */
#define BIRD_X      20          /* fixed horizontal position (px)  */
#define BIRD_W      5
#define BIRD_H      5
#define BIRD_START_Y 24         /* start y (px)                    */

/* ─── physics (Q4 fixed-point) ───────────────────────────────────────────── */
#define FP           4
#define TO_FP(n)    ((n) << FP)
#define FROM_FP(n)  ((n) >> FP)

#define GRAVITY      2          /* FP units added to vy per tick — softer fall */
#define FLAP_VY    (-28)        /* FP upward impulse — gentler, less overshoot */
#define VY_MAX       22         /* FP terminal fall velocity — less punishing */

/* ─── pipes ──────────────────────────────────────────────────────────────── */
#define PIPE_COUNT   2
#define PIPE_W       8          /* pipe width in pixels             */
#define PIPE_SPEED   2          /* pixels per tick (px, not FP)     */
#define PIPE_SPACING 72         /* horizontal gap between pipes — more reaction time */
#define GAP_H        22         /* opening height in pixels — wider for playability */
#define GAP_Y_MIN    8          /* minimum top of gap from field top*/
#define GAP_Y_MAX   (FIELD_H - GAP_H - 6)  /* maximum top of gap — 6px bottom margin */

/* ─── timing ─────────────────────────────────────────────────────────────── */
#define TICK_MS      30         /* ~33 fps physics tick             */

/* ─── score ──────────────────────────────────────────────────────────────── */
/* Bird scores when its left edge passes the right edge of a pipe */
#define SCORE_X     (BIRD_X)    /* compare pipe_x + PIPE_W <= BIRD_X */

/* ─── font (digits + letters needed for HUD) ─────────────────────────────── */
static const uint8_t fl_font[][5] = {
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

static void fl_char(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *g = fl_font[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t b = g[col];
        for (int row = 0; row < 8; row++)
            if (b & (1 << row)) oled_draw_pixel(x + col, y + row);
    }
}

static void fl_str(int x, int y, const char *s)
{
    while (*s) { fl_char(x, y, *s++); x += 6; }
}

/* ─── pipe struct ────────────────────────────────────────────────────────── */
typedef struct {
    int x;          /* left edge of pipe (pixels) */
    int gap_y;      /* top of gap (pixels)        */
    bool scored;    /* true once point awarded     */
} Pipe;

/* ─── state ──────────────────────────────────────────────────────────────── */
static struct {
    int      bird_y_fp;     /* bird y position, fixed-point */
    int      bird_vy_fp;    /* bird y velocity, fixed-point */
    Pipe     pipes[PIPE_COUNT];
    uint32_t score;
    bool     alive;
    bool     btn_last;      /* for rising-edge detection    */
    int64_t  last_tick_ms;
} F;

static inline int64_t fl_now(void) { return esp_timer_get_time() / 1000; }

static int rand_gap_y(void)
{
    return GAP_Y_MIN + (int)(esp_random() % (uint32_t)(GAP_Y_MAX - GAP_Y_MIN + 1));
}

/* ─── AABB collision helper ──────────────────────────────────────────────── */
static bool rects_overlap(int ax, int ay, int aw, int ah,
                           int bx, int by, int bw, int bh)
{
    return !(ax + aw <= bx || bx + bw <= ax ||
             ay + ah <= by || by + bh <= ay);
}

/* ─── public API ─────────────────────────────────────────────────────────── */

void flappy_init(void)
{
    memset(&F, 0, sizeof(F));
    F.bird_y_fp    = TO_FP(BIRD_START_Y);
    F.bird_vy_fp   = 0;
    F.alive        = true;
    F.last_tick_ms = fl_now();

    /* Stagger pipes across the screen */
    for (int i = 0; i < PIPE_COUNT; i++) {
        F.pipes[i].x      = 128 + i * PIPE_SPACING;
        F.pipes[i].gap_y  = rand_gap_y();
        F.pipes[i].scored = false;
    }
}

void flappy_input(int dx, int dy, bool btn)
{
    (void)dx; (void)dy;
    /* Rising edge: flap on button press (not hold) */
    if (btn && !F.btn_last) {
        F.bird_vy_fp = FLAP_VY;
        sound_play(NOTE_E4, 40);   /* quick flap tick — short so rapid flapping doesn't stack delays */
    }
    F.btn_last = btn;
}

bool flappy_tick(uint32_t *score_out)
{
    if (!F.alive) { *score_out = F.score; return false; }

    int64_t now = fl_now();
    if ((now - F.last_tick_ms) < TICK_MS) { *score_out = F.score; return true; }
    F.last_tick_ms = now;

    /* ── gravity ── */
    F.bird_vy_fp += GRAVITY;
    if (F.bird_vy_fp > VY_MAX) F.bird_vy_fp = VY_MAX;
    F.bird_y_fp  += F.bird_vy_fp;

    int bird_y = FROM_FP(F.bird_y_fp);

    /* ── ground / ceiling collision ── */
    if (bird_y + BIRD_H >= GROUND_Y || bird_y < 0) {
        F.alive = false;
        sound_punch(120, 200);   /* low-frequency punch — more impact than a musical note for a crash */
        *score_out = F.score;
        return false;
    }

    /* ── scroll pipes + collision + scoring ── */
    for (int i = 0; i < PIPE_COUNT; i++) {
        F.pipes[i].x -= PIPE_SPEED;

        /* Recycle off-screen pipes */
        if (F.pipes[i].x + PIPE_W < 0) {
            /* Find the rightmost pipe and place this one beyond it */
            int max_x = 0;
            for (int j = 0; j < PIPE_COUNT; j++)
                if (j != i && F.pipes[j].x > max_x)
                    max_x = F.pipes[j].x;
            F.pipes[i].x      = max_x + PIPE_SPACING;
            F.pipes[i].gap_y  = rand_gap_y();
            F.pipes[i].scored = false;
        }

        int px = F.pipes[i].x;
        int gy = F.pipes[i].gap_y;

        /* Collision: top pipe rectangle */
        if (rects_overlap(BIRD_X, bird_y, BIRD_W, BIRD_H,
                          px, 0, PIPE_W, gy)) {
            F.alive = false;
            sound_punch(120, 200);   /* same impact punch as ground hit */
            *score_out = F.score; return false;
        }
        /* Collision: bottom pipe rectangle */
        if (rects_overlap(BIRD_X, bird_y, BIRD_W, BIRD_H,
                          px, gy + GAP_H, PIPE_W, GROUND_Y - gy - GAP_H)) {
            F.alive = false;
            sound_punch(120, 200);
            *score_out = F.score; return false;
        }

        /* Score: bird's right edge clears pipe's right edge */
        if (!F.pipes[i].scored && (BIRD_X + BIRD_W) > (px + PIPE_W)) {
            F.score++;
            F.pipes[i].scored = true;
            /* Single note, not a melody — pipes clear in rapid succession
             * and a longer tune per pipe would start to queue up awkwardly */
            sound_play(NOTE_E5, 50);
        }
    }

    *score_out = F.score;
    return true;
}

void flappy_draw(void)
{
    int bird_y = FROM_FP(F.bird_y_fp);

    /* Ground */
    for (int x = 0; x < 128; x++) oled_draw_pixel(x, GROUND_Y);

    /* Pipes */
    for (int i = 0; i < PIPE_COUNT; i++) {
        int px = F.pipes[i].x;
        int gy = F.pipes[i].gap_y;

        if (px + PIPE_W < 0 || px > 127) continue;

        /* Top pipe — filled */
        for (int y = 0; y < gy; y++)
            for (int x = px; x < px + PIPE_W && x < 128; x++)
                if (x >= 0) oled_draw_pixel(x, y);

        /* Top pipe cap (1px wider each side) */
        for (int x = px - 1; x < px + PIPE_W + 1 && x < 128; x++)
            if (x >= 0 && gy > 0) {
                oled_draw_pixel(x, gy - 1);
            }

        /* Bottom pipe — filled */
        for (int y = gy + GAP_H; y < GROUND_Y; y++)
            for (int x = px; x < px + PIPE_W && x < 128; x++)
                if (x >= 0) oled_draw_pixel(x, y);

        /* Bottom pipe cap */
        for (int x = px - 1; x < px + PIPE_W + 1 && x < 128; x++)
            if (x >= 0 && gy + GAP_H < GROUND_Y) {
                oled_draw_pixel(x, gy + GAP_H);
            }
    }

    /*
     * Bird — 5×5 box with a pointed beak (rightward triangle)
     * and a small tail notch on the left.
     *
     *  .XXXX.   row 0
     *  XXXXXX>  row 1  (beak pixel at col 5)
     *  XXXXXX>  row 2
     *  XXXXX.   row 3
     *  .XXX..   row 4
     */
    for (int dy = 0; dy < BIRD_H; dy++) {
        for (int dx = 0; dx < BIRD_W; dx++) {
            /* Skip corners for rounded look */
            if ((dy == 0 && dx == 0) || (dy == BIRD_H-1 && dx == 0))
                continue;
            oled_draw_pixel(BIRD_X + dx, bird_y + dy);
        }
    }
    /* Beak — one pixel to the right of centre rows */
    oled_draw_pixel(BIRD_X + BIRD_W,     bird_y + 1);
    oled_draw_pixel(BIRD_X + BIRD_W,     bird_y + 2);

    /* HUD */
    char buf[16];
    snprintf(buf, sizeof(buf), "SCORE:%lu", (unsigned long)F.score);
    fl_str(2, HUD_Y, buf);
}