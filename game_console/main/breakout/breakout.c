/*
 * BREAKOUT — paddle + ball + brick grid. 128x160 color version.
 *
 * LAYOUT CHANGE FROM 128x64 MONO:
 *   Old: 4 brick rows crammed into y=2..25, paddle at y=55, tiny HUD
 *   New: 8 brick rows (was 4) using the extra vertical space, much
 *        bigger open play area below the bricks, paddle near the
 *        bottom, dedicated HUD strip at the top.
 *
 * COLOR ADDITION: each brick row gets its own color, the classic
 * "rainbow rows" look from the original Breakout/Arkanoid arcade games.
 * This was impossible in monochrome — every brick looked identical
 * before regardless of row. Now top rows (worth more points) are
 * visually distinct in color, not just position.
 */

#include "breakout.h"
#include "tft.h"
#include "font.h"
#include "palette.h"
#include "effects.h"
#include "esp_timer.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── layout ─────────────────────────────────────────────────────────────── */
#define HUD_H       16
#define B_W         TFT_WIDTH
#define B_H         (TFT_HEIGHT - HUD_H)

#define BRICK_COLS  16
#define BRICK_ROWS  8            /* was 4 — doubled thanks to extra height */
/*
 * BRICK_W/BRICK_GAP recalculated for a perfect fit on the 160px landscape
 * width. Previous values (BRICK_W=7, BRICK_GAP=1) only filled 127px of
 * the 160px play field, leaving ~32px of unused padding on the right —
 * visible in testing photos. BRICK_W=8/BRICK_GAP=2 fills exactly 158px
 * (16 cols * 10px - 2px trailing gap), spanning the full play field
 * between the left and right walls with zero leftover.
 */
#define BRICK_W     8
#define BRICK_H     5            /* was 4 — slightly taller, still fits 8 rows */
#define BRICK_GAP   2
#define BRICK_TOP   (HUD_H + 2)

#define PADDLE_W    24
#define PADDLE_H    3
#define PADDLE_Y    (HUD_H + B_H - 6)
#define BALL_R      3            /* was 2 — slightly bigger ball reads better at this scale */

#define TICK_MS     14
#define FP          4
#define TO_FP(n)    ((n)<<FP)
#define FROM_FP(n)  ((n)>>FP)

/*
 * BALL_SPEED rescaled from 36 to 18 FP units.
 *
 * The previous value (36) was tuned BEFORE the screen was rotated to
 * landscape — at that time the vertical play field was ~144px tall. After
 * rotation, B_H (playfield height) is only TFT_HEIGHT(128) - HUD_H(16) =
 * 112px — about 1.3x shorter. The same raw speed constant now covers that
 * shorter distance ~30% faster than originally intended, which is exactly
 * the "ball is too fast now" symptom — the speed itself wasn't changed,
 * the court it travels across got smaller out from under it.
 *
 * 18 FP units at TICK_MS=14 works out to ~80px/sec, crossing the 112px
 * vertical play field in ~1.4 seconds — a reactable, "snappy but fair"
 * pace appropriate for the new field size. BALL_SPEED_MAX scaled down
 * proportionally from 64 to 32 so the progressive speed-up still feels
 * like a meaningful but not overwhelming increase by the time it caps.
 */
#define BALL_SPEED        18
#define BALL_SPEED_INC     2
#define BALL_SPEED_MAX     32
#define BRICKS_PER_SPEEDUP 8

/* ─── color palette ──────────────────────────────────────────────────────── */
#define COL_BG       PAL_BACKGROUND
#define COL_HUD_TEXT PAL_TEXT
#define COL_HUD_LINE PAL_BORDER
#define COL_WALL     PAL_OUTLINE
#define COL_PADDLE   PAL_BLUE_BRIGHT
#define COL_BALL     PAL_WHITE
#define COL_HEART    PAL_GOLD   /* hearts use the collectible/accent gold,
                                    matching the reference image's heart icons */

/*
 * Brick row colors replaced with an 8-step gradient through the
 * palette's blue family (PAL_BLUE_BRIGHT down to PAL_OUTLINE) instead of
 * the previous rainbow (red/orange/gold/yellow/green/cyan/blue/purple —
 * none of which were part of the cohesive palette). Row 0 (top, worth
 * the most points) is the brightest shade; row 7 (bottom, worth the
 * least) is the darkest — "value" is still communicated visually, now
 * through brightness within one consistent hue rather than disconnected
 * colors.
 */
static const uint16_t BRICK_ROW_COLORS[BRICK_ROWS] = {
    0x7EDF,   /* row 0 — PAL_BLUE_BRIGHT, top, most points */
    0x6E1D,
    0x5D7A,
    0x4CB7,
    0x4415,
    0x3352,
    0x228F,
    0x11ED,   /* row 7 — PAL_OUTLINE, bottom, least points */
};

/* ─── brick bitfield ─────────────────────────────────────────────────────── */
/* 16 cols × 8 rows = 128 bits = 16 bytes. Bit set = brick present. */
static uint8_t s_bricks[16];

static bool brick_get(int col, int row)
{
    int bit = row*BRICK_COLS + col;
    return (s_bricks[bit/8] >> (bit%8)) & 1;
}
static void brick_clear(int col, int row)
{
    int bit = row*BRICK_COLS + col;
    s_bricks[bit/8] &= ~(1<<(bit%8));
}
static int bricks_remaining(void)
{
    int n=0;
    for(int i=0;i<16;i++){uint8_t b=s_bricks[i];while(b){n+=b&1;b>>=1;}}
    return n;
}

/* ─── state ──────────────────────────────────────────────────────────────── */
static struct {
    int bx, by;
    int vx, vy;
    int paddle_x;
    int dx_intent;
    uint32_t score;
    int lives;
    bool alive;
    int64_t last_tick;
    int cur_speed;        /* current ball speed (FP units) — grows over time     */
    int bricks_broken;    /* total bricks destroyed this game, drives speed-ups  */
} BK;

static inline int64_t brk_now(void){ return esp_timer_get_time()/1000; }

static void reset_ball(void)
{
    /*
     * Reset POSITION but preserve the current progressive speed
     * (BK.cur_speed) across life losses — only breakout_init() resets
     * speed back to the base BALL_SPEED. Without this, losing a life
     * would undo all accumulated difficulty, which defeats the purpose
     * of a progressive speed-up.
     */
    BK.bx = TO_FP(B_W/2);
    BK.by = TO_FP(HUD_H + B_H/2);
    BK.vx = BK.cur_speed;
    BK.vy = -BK.cur_speed;
}

void breakout_init(void)
{
    memset(&BK,0,sizeof(BK));
    memset(s_bricks,0xFF,sizeof(s_bricks));
    BK.paddle_x = (B_W - PADDLE_W)/2;
    BK.lives = 3;
    BK.alive = true;
    BK.last_tick = brk_now();
    BK.cur_speed = BALL_SPEED;   /* progressive speed starts at base, grows from here */
    reset_ball();
}

void breakout_input(int dx, int dy, bool btn)
{
    (void)dy;(void)btn;
    BK.dx_intent = dx;
}

bool breakout_tick(uint32_t *score_out)
{
    if(!BK.alive){ *score_out=BK.score; return false; }

    int64_t now=brk_now();
    if((now-BK.last_tick)<TICK_MS) { *score_out=BK.score; return true; }
    BK.last_tick=now;

    BK.paddle_x += BK.dx_intent*3;
    if(BK.paddle_x<1) BK.paddle_x=1;
    if(BK.paddle_x>B_W-1-PADDLE_W) BK.paddle_x=B_W-1-PADDLE_W;

    BK.bx+=BK.vx; BK.by+=BK.vy;
    int bx=FROM_FP(BK.bx), by=FROM_FP(BK.by);

    if(bx<=1){BK.vx=abs(BK.vx);BK.bx=TO_FP(2); sound_play(NOTE_E4, 25);}
    if(bx+BALL_R*2>=B_W-1){BK.vx=-abs(BK.vx);BK.bx=TO_FP(B_W-1-BALL_R*2); sound_play(NOTE_E4, 25);}
    if(by<=HUD_H+1){BK.vy=abs(BK.vy);BK.by=TO_FP(HUD_H+1); sound_play(NOTE_E4, 25);}

    bx=FROM_FP(BK.bx); by=FROM_FP(BK.by);
    if(by+BALL_R*2>=PADDLE_Y && by<=PADDLE_Y+PADDLE_H+2 &&
       bx+BALL_R*2>=BK.paddle_x && bx<=BK.paddle_x+PADDLE_W){
        BK.vy=-abs(BK.vy);
        BK.by=TO_FP(PADDLE_Y-BALL_R*2-1);
        int off=(bx+BALL_R)-(BK.paddle_x+PADDLE_W/2);
        /*
         * BUG FIX: was hardcoded to BALL_SPEED (the flat base constant),
         * which meant every paddle bounce silently reset the ball back
         * to base speed regardless of how many speed-ups had accumulated
         * — the progressive difficulty was being undone on every single
         * hit. Now scales using BK.cur_speed, the ball's actual current
         * speed, so paddle bounces preserve accumulated difficulty.
         */
        BK.vx=(off*BK.cur_speed)/(PADDLE_W/2);
        if(BK.vx==0) BK.vx=1;
        sound_play(NOTE_C5, 35);
    }

    bx=FROM_FP(BK.bx); by=FROM_FP(BK.by);
    int check_pts[4][2]={{bx,by},{bx+BALL_R*2,by},{bx,by+BALL_R*2},{bx+BALL_R*2,by+BALL_R*2}};
    bool hit_v=false, hit_h=false;
    bool brick_hit_this_tick=false;

    for(int p=0;p<4;p++){
        int px=check_pts[p][0], py=check_pts[p][1];
        int bry=py-BRICK_TOP;
        int row=bry/(BRICK_H+BRICK_GAP);
        int col=(px-1)/(BRICK_W+BRICK_GAP);
        if(row<0||row>=BRICK_ROWS||col<0||col>=BRICK_COLS) continue;
        if(!brick_get(col,row)) continue;

        int bx0=1+col*(BRICK_W+BRICK_GAP);
        int by0=BRICK_TOP+row*(BRICK_H+BRICK_GAP);
        int by1=by0+BRICK_H;
        (void)bx0;

        if((BK.vy<0 && py<=by0+1) || (BK.vy>0 && py>=by1-1)) hit_v=true;
        else hit_h=true;

        brick_clear(col,row);
        BK.score += (uint32_t)(BRICK_ROWS - row) * 10;
        brick_hit_this_tick = true;
        BK.bricks_broken++;

        /*
         * Progressive speed-up — every BRICKS_PER_SPEEDUP bricks destroyed,
         * raise cur_speed by BALL_SPEED_INC, capped at BALL_SPEED_MAX so
         * the ball never becomes physically unreactable. The speed boost
         * is applied to vx/vy proportionally (preserving current direction)
         * rather than just overwriting them, so the ball's current angle
         * isn't disrupted by the speed change.
         */
        if (BK.bricks_broken % BRICKS_PER_SPEEDUP == 0 &&
            BK.cur_speed < BALL_SPEED_MAX) {
            int old_speed = BK.cur_speed;
            BK.cur_speed += BALL_SPEED_INC;
            if (BK.cur_speed > BALL_SPEED_MAX) BK.cur_speed = BALL_SPEED_MAX;

            BK.vx = (BK.vx * BK.cur_speed) / old_speed;
            BK.vy = (BK.vy * BK.cur_speed) / old_speed;

            static const Note speedup_tune[] = { { NOTE_G4, 40 }, { NOTE_C5, 60 } };
            sound_play_melody(speedup_tune, sizeof(speedup_tune)/sizeof(speedup_tune[0]));
        }
    }
    if(hit_v) BK.vy=-BK.vy;
    if(hit_h) BK.vx=-BK.vx;

    if (brick_hit_this_tick) sound_play(NOTE_G5, 30);

    if(FROM_FP(BK.by)>HUD_H+B_H){
        BK.lives--;
        if(BK.lives<=0){
            BK.alive=false;
            static const Note gameover_tune[] = {
                { NOTE_A4, 90 }, { NOTE_F4, 90 }, { NOTE_C4, 250 },
            };
            sound_play_melody(gameover_tune, sizeof(gameover_tune)/sizeof(gameover_tune[0]));
            *score_out=BK.score; return false;
        }
        sound_play(NOTE_D4, 200);
        reset_ball();
    }

    if(bricks_remaining()==0){
        BK.score+=100;
        memset(s_bricks,0xFF,sizeof(s_bricks));
        static const Note wave_tune[] = {
            { NOTE_C5, 70 }, { NOTE_E5, 70 }, { NOTE_G5, 70 }, { NOTE_C6, 140 },
        };
        sound_play_melody(wave_tune, sizeof(wave_tune)/sizeof(wave_tune[0]));
    }

    *score_out=BK.score;
    return true;
}

void breakout_draw(void)
{
    tft_fill_rect(0, HUD_H, B_W, B_H, COL_BG);

    /* HUD — score text on the left, heart sprites for lives on the right,
     * matching the reference image's "1UP / hearts" layout rather than a
     * plain "LIVES:%d" text readout. */
    tft_fill_rect(0, 0, B_W, HUD_H, COL_BG);
    char buf[24];
    snprintf(buf,sizeof(buf),"SCORE:%lu",(unsigned long)BK.score);
    font_draw_str(2, 4, buf, COL_HUD_TEXT);

    /* Hearts drawn right-to-left from the screen edge so they stay
     * anchored to the corner regardless of how many lives remain */
    {
        int heart_x = B_W - 9;
        for (int i = 0; i < 3; i++) {
            bool filled = (i < BK.lives);
            effects_draw_heart(heart_x, 4, filled, COL_HEART);
            heart_x -= 9;
        }
    }

    for (int x = 0; x < B_W; x++) tft_draw_pixel(x, HUD_H - 1, COL_HUD_LINE);

    /* Walls */
    for(int x=0;x<B_W;x++) tft_draw_pixel(x, HUD_H,       COL_WALL);
    for(int x=0;x<B_W;x++) tft_draw_pixel(x, HUD_H+B_H-1,  COL_WALL);
    for(int y=HUD_H;y<HUD_H+B_H;y++) tft_draw_pixel(0,     y, COL_WALL);
    for(int y=HUD_H;y<HUD_H+B_H;y++) tft_draw_pixel(B_W-1, y, COL_WALL);

    /* Bricks — solid color fill per row, color from BRICK_ROW_COLORS */
    for(int row=0;row<BRICK_ROWS;row++){
        uint16_t col_color = BRICK_ROW_COLORS[row];
        for(int col=0;col<BRICK_COLS;col++){
            if(!brick_get(col,row)) continue;
            int px=1+col*(BRICK_W+BRICK_GAP);
            int py=BRICK_TOP+row*(BRICK_H+BRICK_GAP);
            tft_fill_rect(px, py, BRICK_W, BRICK_H, col_color);
        }
    }

    /* Ball */
    int bx=FROM_FP(BK.bx), by=FROM_FP(BK.by);
    tft_fill_rect(bx, by, BALL_R*2, BALL_R*2, COL_BALL);

    /* Paddle */
    tft_fill_rect(BK.paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, COL_PADDLE);
}