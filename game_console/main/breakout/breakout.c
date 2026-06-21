/*
 * BREAKOUT — paddle + ball + brick grid.
 *
 * Mechanics distinct from Snake and Pong:
 *   - Destructible bricks stored in a bitfield (memory efficient)
 *   - Ball angle varies by paddle hit position (same as pong but vertical field)
 *   - Lives system (3 lives) before game-over
 *   - Score increases per brick type (top rows worth more)
 *
 * Layout (128×64):
 *   y=0..1     top wall
 *   y=2..25    brick grid  (4 rows × 6 rows of bricks)
 *   y=26..54   open play area
 *   y=55..57   paddle
 *   y=58..63   HUD (score + lives)
 *
 * Bricks: 16 columns × 4 rows = 64 bricks, stored in 8 bytes (bitfield).
 * Each brick is 7px wide × 5px tall with 1px gap.
 */

#include "breakout.h"
#include "oled.h"
#include "esp_timer.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── layout ─────────────────────────────────────────────────────────────── */
#define B_W         126         /* play field width (1px walls each side) */
#define BRICK_COLS  16
#define BRICK_ROWS  4
#define BRICK_W     7           /* brick pixel width  */
#define BRICK_H     4           /* brick pixel height */
#define BRICK_GAP   1
#define BRICK_TOP   2           /* y offset of first brick row */

#define PADDLE_W    22
#define PADDLE_H    2
#define PADDLE_Y    55
#define BALL_R      2           /* ball radius in px */

#define TICK_MS     14
#define FP          4
#define TO_FP(n)    ((n)<<FP)
#define FROM_FP(n)  ((n)>>FP)
#define BALL_SPEED  20

/* ─── font (reuse from pong style) ──────────────────────────────────────── */
static const uint8_t brk_font[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' (index 10) */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' (index 11) */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' (index 12) */
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' (index 13) */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' (index 14) */
};

static void brk_char(int x, int y, char c)
{
    const uint8_t *g = NULL;
    if      (c>='0'&&c<='9') g=brk_font[c-'0'];
    else if (c=='P') g=brk_font[10];
    else if (c=='T') g=brk_font[11];
    else if (c=='S') g=brk_font[12];
    else if (c==' ') g=brk_font[13];
    else if (c==':') g=brk_font[14];
    if(!g) return;
    for(int col=0;col<5;col++){uint8_t b=g[col];for(int r=0;r<8;r++)if(b&(1<<r))oled_draw_pixel(x+col,y+r);}
}
static void brk_str(int x, int y, const char *s)
{ while(*s){brk_char(x,y,*s++);x+=6;} }

/* ─── brick bitfield ─────────────────────────────────────────────────────── */
/* 16 cols × 4 rows = 64 bits = 8 bytes. Bit set = brick present. */
static uint8_t s_bricks[8];   /* [row*2 + col/8] >> (col%8) & 1 */

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
    for(int i=0;i<8;i++){uint8_t b=s_bricks[i];while(b){n+=b&1;b>>=1;}}
    return n;
}

/* ─── state ──────────────────────────────────────────────────────────────── */
static struct {
    int bx, by;        /* ball FP position */
    int vx, vy;        /* ball FP velocity */
    int paddle_x;
    int dx_intent;
    uint32_t score;
    int lives;
    bool alive;
    int64_t last_tick;
} BK;

static inline int64_t brk_now(void){ return esp_timer_get_time()/1000; }

static void reset_ball(void)
{
    BK.bx = TO_FP(64);
    BK.by = TO_FP(45);
    BK.vx = BALL_SPEED;
    BK.vy = -BALL_SPEED;
}

void breakout_init(void)
{
    memset(&BK,0,sizeof(BK));
    /* Set all bricks */
    memset(s_bricks,0xFF,sizeof(s_bricks));
    BK.paddle_x = (128 - PADDLE_W)/2;
    BK.lives = 3;
    BK.alive = true;
    BK.last_tick = brk_now();
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
    if((now-BK.last_tick)<TICK_MS) return true;
    BK.last_tick=now;

    /* Paddle */
    BK.paddle_x += BK.dx_intent*3;
    if(BK.paddle_x<1) BK.paddle_x=1;
    if(BK.paddle_x>127-PADDLE_W) BK.paddle_x=127-PADDLE_W;

    /* Ball */
    BK.bx+=BK.vx; BK.by+=BK.vy;
    int bx=FROM_FP(BK.bx), by=FROM_FP(BK.by);

    /* Side walls */
    if(bx<=1){BK.vx=abs(BK.vx);BK.bx=TO_FP(2); sound_play(NOTE_E4, 25);}
    if(bx+BALL_R*2>=127){BK.vx=-abs(BK.vx);BK.bx=TO_FP(127-BALL_R*2); sound_play(NOTE_E4, 25);}
    /* Top */
    if(by<=1){BK.vy=abs(BK.vy);BK.by=TO_FP(2); sound_play(NOTE_E4, 25);}

    /* Paddle */
    bx=FROM_FP(BK.bx); by=FROM_FP(BK.by);
    if(by+BALL_R*2>=PADDLE_Y && by<=PADDLE_Y+PADDLE_H+2 &&
       bx+BALL_R*2>=BK.paddle_x && bx<=BK.paddle_x+PADDLE_W){
        BK.vy=-abs(BK.vy);
        BK.by=TO_FP(PADDLE_Y-BALL_R*2-1);
        int off=(bx+BALL_R)-(BK.paddle_x+PADDLE_W/2);
        BK.vx=(off*BALL_SPEED)/(PADDLE_W/2);
        if(BK.vx==0) BK.vx=1;
        sound_play(NOTE_C5, 35);   /* slightly higher pitch than wall bounce */
    }

    /* Brick collision — check the 4 corners of ball AABB */
    bx=FROM_FP(BK.bx); by=FROM_FP(BK.by);
    int check_pts[4][2]={{bx,by},{bx+BALL_R*2,by},{bx,by+BALL_R*2},{bx+BALL_R*2,by+BALL_R*2}};
    bool hit_v=false, hit_h=false;
    bool brick_hit_this_tick=false;   /* prevents duplicate sounds when the
                                        * 4-corner check clears >1 brick in
                                        * the same tick — only one sound */

    for(int p=0;p<4;p++){
        int px=check_pts[p][0], py=check_pts[p][1];
        /* Convert pixel to brick coords */
        int bry=py-BRICK_TOP;
        int row=bry/(BRICK_H+BRICK_GAP);
        int col=(px-1)/(BRICK_W+BRICK_GAP);
        if(row<0||row>=BRICK_ROWS||col<0||col>=BRICK_COLS) continue;
        if(!brick_get(col,row)) continue;

        /* Which edge was hit? */
        int bx0=1+col*(BRICK_W+BRICK_GAP);
        int by0=BRICK_TOP+row*(BRICK_H+BRICK_GAP);
        int by1=by0+BRICK_H;
        (void)bx0;   /* used only for side-hit detection via hit_h fallthrough */

        /* Incoming from below/above → vertical bounce */
        if((BK.vy<0 && py<=by0+1) || (BK.vy>0 && py>=by1-1)) hit_v=true;
        else hit_h=true;

        brick_clear(col,row);
        /* Top rows worth more */
        BK.score += (uint32_t)(BRICK_ROWS - row) * 10;
        brick_hit_this_tick = true;
    }
    if(hit_v) BK.vy=-BK.vy;
    if(hit_h) BK.vx=-BK.vx;

    if (brick_hit_this_tick) {
        /* Bright, percussive — distinct from the lower-pitched wall/paddle
         * bounces so brick destruction reads as "progress" not just "bounce" */
        sound_play(NOTE_G5, 30);
    }

    /* Lost ball */
    if(FROM_FP(BK.by)>64){
        BK.lives--;
        if(BK.lives<=0){
            BK.alive=false;
            static const Note gameover_tune[] = {
                { NOTE_A4, 90 }, { NOTE_F4, 90 }, { NOTE_C4, 250 },
            };
            sound_play_melody(gameover_tune, sizeof(gameover_tune)/sizeof(gameover_tune[0]));
            *score_out=BK.score; return false;
        }
        /* Lost a life but still have lives left — lower, single "ouch"
         * tone distinct from full game-over so the player can tell the
         * difference by ear (this is a setback, not the end) */
        sound_play(NOTE_D4, 200);
        reset_ball();
    }

    /* Win — all bricks cleared */
    if(bricks_remaining()==0){
        BK.score+=100;
        memset(s_bricks,0xFF,sizeof(s_bricks)); /* new wave */
        /* Triumphant ascending run — clearly different from both the brick-hit
         * tick and the game-over stings, marks a genuine milestone */
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
    /* Walls */
    for(int x=0;x<128;x++) oled_draw_pixel(x,0);
    for(int x=0;x<128;x++) oled_draw_pixel(x,57);
    for(int y=0;y<58;y++)  oled_draw_pixel(0,y);
    for(int y=0;y<58;y++)  oled_draw_pixel(127,y);

    /* Bricks */
    for(int row=0;row<BRICK_ROWS;row++){
        for(int col=0;col<BRICK_COLS;col++){
            if(!brick_get(col,row)) continue;
            int px=1+col*(BRICK_W+BRICK_GAP);
            int py=BRICK_TOP+row*(BRICK_H+BRICK_GAP);
            /* Solid fill with 1px outline-only for top 2 rows (worth more) */
            if(row<2){
                for(int x=px;x<px+BRICK_W;x++){oled_draw_pixel(x,py);oled_draw_pixel(x,py+BRICK_H-1);}
                for(int y=py;y<py+BRICK_H;y++){oled_draw_pixel(px,y);oled_draw_pixel(px+BRICK_W-1,y);}
            } else {
                for(int y=py;y<py+BRICK_H;y++)
                    for(int x=px;x<px+BRICK_W;x++)
                        oled_draw_pixel(x,y);
            }
        }
    }

    /* Ball */
    int bx=FROM_FP(BK.bx), by=FROM_FP(BK.by);
    for(int dy=0;dy<BALL_R*2;dy++)
        for(int dx=0;dx<BALL_R*2;dx++)
            oled_draw_pixel(bx+dx,by+dy);

    /* Paddle */
    for(int x=BK.paddle_x;x<BK.paddle_x+PADDLE_W;x++){
        oled_draw_pixel(x,PADDLE_Y);
        oled_draw_pixel(x,PADDLE_Y+1);
    }

    /* HUD */
    char buf[24];
    snprintf(buf,sizeof(buf),"S:%lu  T:%d",(unsigned long)BK.score,BK.lives);
    brk_str(2,58,buf);
}