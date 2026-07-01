/*
 * PONG — single-player wall-bounce variant. 128x160 color version.
 *
 * LAYOUT CHANGE FROM 128x64 MONO:
 *   Old: P_H=56 play field, paddle at y=54, HUD crammed at y=57
 *   New: P_H=144 play field (the extra 96px is almost entirely given to
 *        vertical play space), paddle at y=140, dedicated 16px HUD strip
 *        at the top instead of squeezed text at the very bottom.
 *
 * The much taller field changes the FEEL of Pong meaningfully — the ball
 * travels much further between paddle and top wall, giving more reaction
 * time despite the same physics constants. BALL_SPEED_0 was bumped up
 * slightly to compensate so rounds don't feel sluggish on the taller court.
 */

#include "pong.h"
#include "tft.h"
#include "font.h"
#include "palette.h"
#include "esp_timer.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── layout ─────────────────────────────────────────────────────────────── */
#define HUD_H       16           /* dedicated HUD strip at top      */
#define P_W         TFT_WIDTH    /* 128                              */
#define P_H         (TFT_HEIGHT - HUD_H)  /* 144 — play field height */
#define PADDLE_Y    (HUD_H + P_H - 6)      /* near the bottom of field */
#define PADDLE_H    3
#define PADDLE_W    24
#define BALL_SIZE   4

/* ─── color palette ──────────────────────────────────────────────────────── */
/*
 * Color block switched to the shared palette. COL_WALL specifically was
 * reported as rendering bright green on actual hardware despite being
 * coded as TFT_BLUE — replacing it with an explicit palette reference
 * (PAL_OUTLINE) removes the ambiguity and brings the boundary in line
 * with the same border color used everywhere else in the console.
 */
#define COL_BG       PAL_BACKGROUND
#define COL_HUD_TEXT PAL_TEXT
#define COL_HUD_LINE PAL_BORDER
#define COL_WALL     PAL_OUTLINE
#define COL_PADDLE   PAL_BLUE_BRIGHT
#define COL_BALL     PAL_GOLD

/* ─── fixed-point (<<4 = ×16 sub-pixel) ─────────────────────────────────── */
#define FP          4
#define TO_FP(n)    ((n) << FP)
#define FROM_FP(n)  ((n) >> FP)

/* ─── tuning ─────────────────────────────────────────────────────────────── */
#define TICK_MS         16
#define BALL_SPEED_0    28      /* was 24 — slightly faster to suit the taller court */
#define BALL_SPEED_INC  2
#define HIT_INTERVAL    5
#define PADDLE_SPEED    4       /* was 3 — paddle needs to cover more relative width on wider feel */

/* ─── state ──────────────────────────────────────────────────────────────── */
static struct {
    int  bx, by;
    int  vx, vy;
    int  paddle_x;
    int  dx_intent;
    uint32_t score;
    int  hits;
    int  speed;
    bool alive;
    int64_t last_tick;
} P;

static inline int64_t pong_now(void){ return esp_timer_get_time()/1000; }

void pong_init(void)
{
    memset(&P,0,sizeof(P));
    P.paddle_x = (P_W - PADDLE_W) / 2;
    P.bx = TO_FP(P_W/2);
    P.by = TO_FP(HUD_H + P_H/2);
    P.speed = BALL_SPEED_0;
    P.vx =  P.speed;
    P.vy = -P.speed;
    P.alive = true;
    P.last_tick = pong_now();
}

void pong_input(int dx, int dy, bool btn)
{
    (void)dy; (void)btn;
    P.dx_intent = dx;
}

bool pong_tick(uint32_t *score_out)
{
    if (!P.alive) { *score_out = P.score; return false; }

    int64_t now = pong_now();
    if ((now - P.last_tick) < TICK_MS) { *score_out = P.score; return true; }
    P.last_tick = now;

    P.paddle_x += P.dx_intent * PADDLE_SPEED;
    if (P.paddle_x < 1)              P.paddle_x = 1;
    if (P.paddle_x > P_W-1-PADDLE_W) P.paddle_x = P_W-1-PADDLE_W;

    P.bx += P.vx;
    P.by += P.vy;

    int bpx = FROM_FP(P.bx);
    int bpy = FROM_FP(P.by);

    if (bpx <= 1) { P.vx = abs(P.vx); P.bx = TO_FP(1); sound_play(NOTE_E4, 25); }
    if (bpx + BALL_SIZE >= P_W-1) { P.vx = -abs(P.vx); P.bx = TO_FP(P_W-1-BALL_SIZE); sound_play(NOTE_E4, 25); }
    if (bpy <= HUD_H+1) { P.vy = abs(P.vy); P.by = TO_FP(HUD_H+1); sound_play(NOTE_E4, 25); }

    bpx = FROM_FP(P.bx); bpy = FROM_FP(P.by);
    bool paddle_hit = (bpy + BALL_SIZE >= PADDLE_Y) &&
                      (bpy + BALL_SIZE <= PADDLE_Y + PADDLE_H + 2) &&
                      (bpx + BALL_SIZE >= P.paddle_x) &&
                      (bpx <= P.paddle_x + PADDLE_W);

    if (paddle_hit) {
        P.vy = -abs(P.vy);
        P.by = TO_FP(PADDLE_Y - BALL_SIZE - 1);
        int centre_offset = (bpx + BALL_SIZE/2) - (P.paddle_x + PADDLE_W/2);
        P.vx = (centre_offset * P.speed) / (PADDLE_W/2);
        if (P.vx == 0) P.vx = 1;

        P.score += 5;
        P.hits++;
        sound_play(NOTE_C5, 35);

        if ((P.hits % HIT_INTERVAL) == 0) {
            P.speed += BALL_SPEED_INC;
            P.vx = (P.vx > 0) ? P.speed : -P.speed;
            P.vy = -P.speed;
            static const Note speedup_tune[] = { { NOTE_G4, 40 }, { NOTE_C5, 60 } };
            sound_play_melody(speedup_tune, sizeof(speedup_tune)/sizeof(speedup_tune[0]));
        }
    }

    if (FROM_FP(P.by) > HUD_H + P_H + 4) {
        P.alive = false;
        static const Note miss_tune[] = {
            { NOTE_A4, 90 }, { NOTE_F4, 90 }, { NOTE_C4, 250 },
        };
        sound_play_melody(miss_tune, sizeof(miss_tune)/sizeof(miss_tune[0]));
        *score_out = P.score;
        return false;
    }

    *score_out = P.score;
    return true;
}

void pong_draw(void)
{
    tft_fill_rect(0, HUD_H, P_W, P_H, COL_BG);

    /* HUD */
    tft_fill_rect(0, 0, P_W, HUD_H, COL_BG);
    char buf[20];
    snprintf(buf, sizeof(buf), "HITS:%d  SCORE:%lu", (int)P.hits, (unsigned long)P.score);
    font_draw_str(2, 4, buf, COL_HUD_TEXT);
    for (int x = 0; x < P_W; x++) tft_draw_pixel(x, HUD_H - 1, COL_HUD_LINE);

    /* Walls */
    for(int x=0;x<P_W;x++) tft_draw_pixel(x, HUD_H,        COL_WALL);
    for(int x=0;x<P_W;x++) tft_draw_pixel(x, HUD_H+P_H-1,  COL_WALL);
    for(int y=HUD_H;y<HUD_H+P_H;y++) tft_draw_pixel(0,     y, COL_WALL);
    for(int y=HUD_H;y<HUD_H+P_H;y++) tft_draw_pixel(P_W-1, y, COL_WALL);

    /* Paddle */
    tft_fill_rect(P.paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, COL_PADDLE);

    /* Ball */
    int bx=FROM_FP(P.bx), by=FROM_FP(P.by);
    tft_fill_rect(bx, by, BALL_SIZE, BALL_SIZE, COL_BALL);
}