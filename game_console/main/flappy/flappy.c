/*
 * FLAPPY — gravity-based side-scroller. 128x160 color version.
 *
 * LAYOUT CHANGE FROM 128x64 MONO:
 *   Old: FIELD_H=56, very cramped — bird had ~50px of vertical room
 *   New: FIELD_H=144, the entire extra 96px goes to flight room. This
 *        is the single biggest gameplay improvement from the screen
 *        upgrade — Flappy genuinely plays better with more vertical
 *        space to react in, independent of any color addition.
 *
 * GAP_Y_MAX/MIN and gravity/flap constants are recalculated for the
 * taller field — the relative "feel" (how much of the field height the
 * gap takes up) is kept similar to the old version rather than just
 * scaling pixel-for-pixel, since a literal 2.5x scale-up of GAP_H would
 * make the game trivially easy.
 *
 * BACKGROUND PARALLAX (new):
 * Two extra scrolling layers behind the pipes for visual depth —
 * distant hills (a procedural zigzag silhouette, no storage needed,
 * computed directly from x each frame) and a small fixed set of clouds
 * that drift at roughly half pipe speed. Both layers cost very little:
 * CLOUD_COUNT clouds are only 3 filled rects each, and the hill strip is
 * one fill_rect per screen column segment — comfortably within the
 * existing per-frame draw budget the pipes/bird already use.
 */

#include "flappy.h"
#include "tft.h"
#include "font.h"
#include "palette.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>

/* ─── layout ─────────────────────────────────────────────────────────────── */
#define HUD_H 16
#define FIELD_H (TFT_HEIGHT - HUD_H - 8) /* leave 8px for ground strip */
#define GROUND_Y (HUD_H + FIELD_H)

/* ─── color palette ──────────────────────────────────────────────────────── */
#define COL_BG PAL_BG_PANEL /* was TFT_SKYBLUE — now a dusk-toned \
                                navy/blue sky consistent with the   \
                                rest of the console's palette   */
#define COL_HUD_TEXT PAL_TEXT
#define COL_HUD_BG PAL_BG_DARK
#define COL_GROUND PAL_OUTLINE
#define COL_PIPE PAL_GREEN_MAIN
#define COL_PIPE_CAP PAL_GREEN_MAIN
#define COL_BIRD_BODY PAL_GOLD
#define COL_BIRD_BEAK TFT_ORANGE /* kept outside the palette on purpose — \
                                    the beak is a small accent detail,      \
                                    not a structural UI element, and a      \
                                    warm orange reads clearly against       \
                                    the cool blue bird body            */
#define COL_BIRD_EYE PAL_BG_DARK
#define COL_CLOUD PAL_BLUE_BRIGHT
#define COL_HILL PAL_FOREST
/* ─── bird ───────────────────────────────────────────────────────────────── */
#define BIRD_X 24
#define BIRD_W 9 /* was 5 — bigger bird for the bigger screen */
#define BIRD_H 9
#define BIRD_START_Y (HUD_H + FIELD_H / 2)

/* ─── physics (Q4 fixed-point) ───────────────────────────────────────────── */
#define FP 4
#define TO_FP(n) ((n) << FP)
#define FROM_FP(n) ((n) >> FP)

#define GRAVITY 3 /* re-tuned for the taller field */
#define FLAP_VY (-46)
#define VY_MAX 36

/* ─── pipes ──────────────────────────────────────────────────────────────── */
#define PIPE_COUNT 2
#define PIPE_W 14 /* was 8 — wider to match the bigger bird   */
#define PIPE_SPEED 3
#define PIPE_SPACING 110
#define GAP_H 46 /* scaled to keep similar relative difficulty */
#define GAP_Y_MIN (HUD_H + 12)
#define GAP_Y_MAX (GROUND_Y - GAP_H - 12)

#define TICK_MS 30

/* ─── background parallax ───────────────────────────────────────────────── */
#define CLOUD_COUNT 3
#define CLOUD_W 24
#define CLOUD_H 10
#define CLOUD_SPEED_DIV 3 /* clouds move at PIPE_SPEED/CLOUD_SPEED_DIV — \
                              slower than pipes for a parallax depth cue */
#define GRASS_H 4
#define HILL_H 16
#define HILL_PERIOD 48

/* ─── pipe struct ────────────────────────────────────────────────────────── */
typedef struct
{
    int x;
    int gap_y;
    bool scored;
} Pipe;

/* ─── cloud struct ───────────────────────────────────────────────────────── */
typedef struct
{
    int16_t x, y;
} Cloud;

/* ─── state ──────────────────────────────────────────────────────────────── */
static struct
{
    int bird_y_fp;
    int bird_vy_fp;
    Pipe pipes[PIPE_COUNT];
    Cloud clouds[CLOUD_COUNT];
    int hill_scroll_x; /* accumulated hill parallax offset */
    uint32_t score;
    bool alive;
    bool btn_last;
    int64_t last_tick_ms;
} F;

static inline int64_t fl_now(void) { return esp_timer_get_time() / 1000; }

static int rand_gap_y(void)
{
    return GAP_Y_MIN + (int)(esp_random() % (uint32_t)(GAP_Y_MAX - GAP_Y_MIN + 1));
}

static bool rects_overlap(int ax, int ay, int aw, int ah,
                          int bx, int by, int bw, int bh)
{
    return !(ax + aw <= bx || bx + bw <= ax ||
             ay + ah <= by || by + bh <= ay);
}
/*
 * Triangle wave returning values in [0,255].
 * Used to generate procedural rolling hills.
 */
static int tri_wave(int x, int period)
{
    x %= period;
    if (x < 0)
        x += period;

    int half = period / 2;

    if (x < half)
        return (x * 255) / half;

    return ((period - x) * 255) / half;
}
/*
 * Cloud sprite — a 3-lobe puffy cloud silhouette built from 3 overlapping
 * filled rects (a wide base + two rounded-looking top puffs). Cheap to
 * draw (3 fill_rect calls) and reads clearly as a cloud shape at this
 * scale without needing a full bitmap.
 */
static void draw_cloud(int x, int y)
{
    if (x + CLOUD_W < 0 || x > TFT_WIDTH)
        return;                                     /* off-screen, skip */
    tft_fill_rect(x + 4, y + 4, 16, 6, COL_CLOUD);  /* base body */
    tft_fill_rect(x + 0, y + 0, 10, 8, COL_CLOUD);  /* left puff  */
    tft_fill_rect(x + 12, y + 0, 12, 8, COL_CLOUD); /* right puff */
}

/*
 * Distant hill silhouette — drawn procedurally from a simple triangular
 * zigzag pattern rather than stored as a bitmap or point array, so it
 * costs zero extra static memory. hill_scroll_x shifts the pattern's
 * phase each frame for a slow parallax scroll independent of the pipes.
 * Drawn as a sequence of narrow vertical fill_rect columns, each column's
 * height following the zigzag — visually reads as rolling hills at a
 * fraction of the cost of a full bitmap silhouette.
 */
static void draw_hills(int scroll_x)
{
    const int base_y = GROUND_Y - GRASS_H - HILL_H;
    const int period_far = 48;
    const int amp_far = 10;
    const int period_near = 24;
    const int amp_near = 6;

    for (int x = 0; x < TFT_WIDTH; x++)
    {
        int wx = x + scroll_x;
        // far mountain layer (lower, smoother)
        int h_far =
            (tri_wave(wx, period_far) * amp_far) / 255;

        int h_near =
            (tri_wave(wx + period_far / 2,
                      period_near) *
             amp_near) /
            255;
        int y_far = base_y + h_far;
        tft_draw_line(
            x,
            y_far,
            x,
            GROUND_Y - GRASS_H,
            PAL_FOREST);

        // near tree line (higher, more detail)
        int y_near = y_far - (HILL_H / 2) + h_near;
        if (y_near < base_y - HILL_H)
            y_near = base_y - HILL_H;
        tft_draw_line(
            x,
            y_near,
            x,
            GROUND_Y - GRASS_H,
            PAL_GREEN_DARK);
        // int phase = (col + scroll_x) % HILL_PERIOD;
        /* Triangular wave: rises for first half of period, falls for
         * second half — cheap integer math, no trig needed. */
        /*   int half = HILL_PERIOD / 2;
           int height = (phase < half)
                            ? (phase * HILL_AMPLITUDE) / half
                            : ((HILL_PERIOD - phase) * HILL_AMPLITUDE) / half;
           int col_h = HILL_H - height;
           tft_fill_rect(col, base_y + height, 2, col_h, COL_HILL);
           */
    }
}
static void draw_ground(void)
{
    // grass top
    tft_fill_rect(0, GROUND_Y, TFT_WIDTH, GRASS_H, PAL_GREEN_MAIN);

    // grass blades
    for (int x = 0; x < TFT_WIDTH; x += 2)
    {
        int h = 2 + (x & 2);
        int y0 = GROUND_Y + GRASS_H - 1;
        int y1 = GROUND_Y + GRASS_H - 1 - h;
        if (y1 < GROUND_Y)
        {
            y1 = GROUND_Y;
        }
        tft_draw_line(x, y0, x, y1, PAL_GREEN_LIGHT);
        if (x & 1)
        {
            tft_draw_pixel(x, y1, PAL_GREEN_GRASS);
        }
    }
    // soil base
    tft_fill_rect(0, GROUND_Y + GRASS_H, TFT_WIDTH, TFT_HEIGHT - (GROUND_Y + GRASS_H), PAL_BROWN);

    // soil texture (darker clumps)
    for (int y = GROUND_Y + GRASS_H + 2; y < TFT_HEIGHT; y += 4)
    {
        for (int x = (y / 4) & 1 ? 2 : 0; x < TFT_WIDTH; x += 8)
        {
            int w = (x + 4 < TFT_WIDTH) ? 4 : (TFT_WIDTH - x);
            tft_fill_rect(x, y, w, 2, PAL_BROWN_DARK);
        }
    }
}

void flappy_init(void)
{
    memset(&F, 0, sizeof(F));
    F.bird_y_fp = TO_FP(BIRD_START_Y);
    F.bird_vy_fp = 0;
    F.alive = true;
    F.last_tick_ms = fl_now();

    for (int i = 0; i < PIPE_COUNT; i++)
    {
        F.pipes[i].x = TFT_WIDTH + i * PIPE_SPACING;
        F.pipes[i].gap_y = rand_gap_y();
        F.pipes[i].scored = false;
    }

    /* Spread clouds evenly across the screen width at boot so they don't
     * all pop in from the right edge one at a time — gives an already-
     * populated sky on the very first frame instead of an empty one. */
    for (int i = 0; i < CLOUD_COUNT; i++)
    {
        F.clouds[i].x = (int16_t)((i * TFT_WIDTH) / CLOUD_COUNT);
        F.clouds[i].y = (int16_t)(HUD_H + 6 + (int)(esp_random() % 20));
    }
    F.hill_scroll_x = 0;
}

void flappy_input(int dx, int dy, bool btn)
{
    (void)dx;
    (void)dy;
    if (btn && !F.btn_last)
    {
        F.bird_vy_fp = FLAP_VY;
        sound_play(NOTE_E4, 40);
    }
    F.btn_last = btn;
}

bool flappy_tick(uint32_t *score_out)
{
    if (!F.alive)
    {
        *score_out = F.score;
        return false;
    }

    int64_t now = fl_now();
    if ((now - F.last_tick_ms) < TICK_MS)
    {
        *score_out = F.score;
        return true;
    }
    F.last_tick_ms = now;

    F.bird_vy_fp += GRAVITY;
    if (F.bird_vy_fp > VY_MAX)
        F.bird_vy_fp = VY_MAX;
    F.bird_y_fp += F.bird_vy_fp;

    /*
     * Background parallax scroll — clouds move slower than pipes
     * (PIPE_SPEED / CLOUD_SPEED_DIV), hills scroll at their own fixed
     * rate. Both wrap independently of the pipe/collision logic below,
     * since they're purely decorative and never interact with gameplay.
     */
    for (int i = 0; i < CLOUD_COUNT; i++)
    {
        int cloud_speed = PIPE_SPEED / CLOUD_SPEED_DIV;
        if (cloud_speed < 1)
            cloud_speed = 1; /* guard against future
                                 constant changes making
                                 this divide to zero */
        F.clouds[i].x -= cloud_speed;
        if (F.clouds[i].x + CLOUD_W < 0)
        {
            F.clouds[i].x = TFT_WIDTH;
            F.clouds[i].y = (int16_t)(HUD_H + 6 + (int)(esp_random() % 20));
        }
    }
    F.hill_scroll_x = (F.hill_scroll_x + 1) % HILL_PERIOD;

    int bird_y = FROM_FP(F.bird_y_fp);

    if (bird_y + BIRD_H >= GROUND_Y || bird_y < HUD_H)
    {
        F.alive = false;
        sound_punch(120, 200);
        *score_out = F.score;
        return false;
    }

    for (int i = 0; i < PIPE_COUNT; i++)
    {
        F.pipes[i].x -= PIPE_SPEED;

        if (F.pipes[i].x + PIPE_W < 0)
        {
            int max_x = 0;
            for (int j = 0; j < PIPE_COUNT; j++)
                if (j != i && F.pipes[j].x > max_x)
                    max_x = F.pipes[j].x;
            F.pipes[i].x = max_x + PIPE_SPACING;
            F.pipes[i].gap_y = rand_gap_y();
            F.pipes[i].scored = false;
        }

        int px = F.pipes[i].x;
        int gy = F.pipes[i].gap_y;

        if (rects_overlap(BIRD_X, bird_y, BIRD_W, BIRD_H,
                          px, HUD_H, PIPE_W, gy - HUD_H))
        {
            F.alive = false;
            sound_punch(120, 200);
            *score_out = F.score;
            return false;
        }
        if (rects_overlap(BIRD_X, bird_y, BIRD_W, BIRD_H,
                          px, gy + GAP_H, PIPE_W, GROUND_Y - gy - GAP_H))
        {
            F.alive = false;
            sound_punch(120, 200);
            *score_out = F.score;
            return false;
        }

        if (!F.pipes[i].scored && (BIRD_X + BIRD_W) > (px + PIPE_W))
        {
            F.score++;
            F.pipes[i].scored = true;
            sound_play(NOTE_E5, 50);
        }
    }

    *score_out = F.score;
    return true;
}

void flappy_draw(void)
{
    int bird_y = FROM_FP(F.bird_y_fp);

    /* Sky background + HUD */
    tft_fill_rect(0, HUD_H, TFT_WIDTH, FIELD_H, COL_BG);
    tft_fill_rect(0, 0, TFT_WIDTH, HUD_H, COL_HUD_BG);

    /* Background parallax layers — drawn back-to-front: distant hills
     * first (furthest away, slowest apparent motion), then clouds
     * (nearer, drawn on top of hills), then ground/pipes/bird on top of
     * everything as before. This ordering is what actually sells the
     * depth illusion — closer layers must occlude farther ones. */
    draw_hills(F.hill_scroll_x);

    for (int i = 0; i < CLOUD_COUNT; i++)
    {
        draw_cloud(F.clouds[i].x, F.clouds[i].y);
    }
    draw_ground();

    /* Pipes */
    for (int i = 0; i < PIPE_COUNT; i++)
    {
        int px = F.pipes[i].x;
        int gy = F.pipes[i].gap_y;

        if (px + PIPE_W < 0 || px > TFT_WIDTH)
            continue;

        /* Top pipe body */
        tft_fill_rect(px, HUD_H, PIPE_W, gy - HUD_H, COL_PIPE);
        /* Top pipe cap — slightly wider, darker green */
        tft_fill_rect(px - 2, gy - 4, PIPE_W + 4, 4, COL_PIPE_CAP);

        /* Bottom pipe body */
        tft_fill_rect(px, gy + GAP_H, PIPE_W, GROUND_Y - gy - GAP_H, COL_PIPE);
        /* Bottom pipe cap */
        tft_fill_rect(px - 2, gy + GAP_H, PIPE_W + 4, 4, COL_PIPE_CAP);
    }

    /* Bird body — yellow rounded box */
    tft_fill_rect(BIRD_X, bird_y, BIRD_W, BIRD_H, COL_BIRD_BODY);
    /* Beak — orange triangle-ish block to the right */
    tft_fill_rect(BIRD_X + BIRD_W, bird_y + BIRD_H / 2 - 1, 3, 3, COL_BIRD_BEAK);
    /* Eye */
    tft_draw_pixel(BIRD_X + BIRD_W - 3, bird_y + 2, COL_BIRD_EYE);

    /* HUD text */
    char buf[16];
    snprintf(buf, sizeof(buf), "SCORE: %lu", (unsigned long)F.score);
    font_draw_str(2, 4, buf, COL_HUD_TEXT);
}