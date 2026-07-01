/*
 * SPACE INVADERS — 128x160 color version.
 *
 * LAYOUT CHANGE FROM 128x64 MONO:
 *   Old: 3 alien rows (already a fix for a geometry bug — see history),
 *        FIELD_BOT=54, very little vertical room for the march-and-
 *        descend mechanic to breathe.
 *   New: 6 alien rows (back to a "real" Invaders row count, now with
 *        ENOUGH headroom that the geometry bug from before can't recur)
 *        plus a much taller field overall — FIELD_BOT moved from 54 to
 *        ~144, giving roughly 2.5x more descent distance before the
 *        aliens reach the player.
 *
 * COLOR: each alien row gets a distinct color (classic arcade look —
 * different colored rows feel like different "ranks" of enemy), and
 * the player cannon and bullets get their own theme colors too.
 */

#include "spaceinvaders.h"
#include "tft.h"
#include "font.h"
#include "palette.h"
#include "effects.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>

/* ─── layout ─────────────────────────────────────────────────────────────── */
#define HUD_H        16
#define FIELD_TOP    (HUD_H + 2)
#define FIELD_BOT    (TFT_HEIGHT - 16)   /* leave room for player cannon zone */
#define PLAYER_Y     (TFT_HEIGHT - 10)
#define PLAYER_W     14
#define PLAYER_H     6

/* ─── alien grid ─────────────────────────────────────────────────────────── */
#define ALIEN_COLS   8
#define ALIEN_ROWS   6            /* was 3 — full row count restored thanks to extra height */
#define ALIEN_W      10
#define ALIEN_H       6
#define ALIEN_GAP_X   5
#define ALIEN_GAP_Y   5
#define ALIEN_STEP_X  2
#define ALIEN_STEP_Y  3
#define ALIEN_START_X 8
#define ALIEN_START_Y FIELD_TOP

#define CELL_W       (ALIEN_W + ALIEN_GAP_X)
#define CELL_H       (ALIEN_H + ALIEN_GAP_Y)

/* ─── color palette ──────────────────────────────────────────────────────── */
#define COL_BG        PAL_BACKGROUND
#define COL_HUD_TEXT  PAL_TEXT
#define COL_HUD_LINE  PAL_BORDER
#define COL_GROUND    PAL_OUTLINE
#define COL_PLAYER    PAL_BLUE_BRIGHT
#define COL_PBULLET   PAL_GOLD
#define COL_ABULLET   PAL_DANGER   /* enemy fire stays red/danger-coded,
                                       intentionally outside the cohesive
                                       blue palette so it reads as a threat */
#define COL_HEART     PAL_GOLD

/*
 * Alien row colors — was TFT_RED/TFT_ORANGE/TFT_GREEN (a rainbow, not
 * part of the cohesive palette at all). Replaced with three shades from
 * palette.h's own blue family: top rows (worth most) get the brightest
 * blue, bottom rows (worth least) get the darkest. Visual "value" is now
 * communicated by brightness within one consistent hue instead of
 * jumping between unrelated colors.
 */
static const uint16_t ALIEN_ROW_COLORS[ALIEN_ROWS] = {
    PAL_BLUE_BRIGHT, PAL_BLUE_BRIGHT,   /* rows 0-1: top, worth most   */
    PAL_BLUE_MAIN,   PAL_BLUE_MAIN,     /* rows 2-3: mid               */
    PAL_OUTLINE,     PAL_OUTLINE,       /* rows 4-5: bottom, worth least */
};

/* ─── bullets ────────────────────────────────────────────────────────────── */
#define BULLET_H      6
#define PLAYER_BULLET_SPEED  4
#define ALIEN_BULLET_SPEED   2
#define MAX_ALIEN_BULLETS    4

/* ─── timing ─────────────────────────────────────────────────────────────── */
#define TICK_MS          20
#define MARCH_INTERVAL   350
#define ALIEN_FIRE_ODDS  10

/* ─── scoring ────────────────────────────────────────────────────────────── */
#define SCORE_TOP_ROW    30
#define SCORE_MID_ROW    20
#define SCORE_BOT_ROW    10

/* ─── alien sprite bitmaps (unchanged shapes, now drawn in color) ────────── */
static const uint8_t ALIEN_A[ALIEN_W] = {
    0b00001100,0b00010010,0b00111110,0b00101010,0b00111110,
    0b00101010,0b00111110,0b00010010,0b00001100,0b00000000,
};
static const uint8_t ALIEN_B[ALIEN_W] = {
    0b00010000,0b00101000,0b00111100,0b00010100,0b00111100,
    0b00010100,0b00111100,0b00101000,0b00010000,0b00000000,
};
static const uint8_t ALIEN_A2[ALIEN_W] = {
    0b00001100,0b00110010,0b00111110,0b00101010,0b00111110,
    0b00101010,0b00111110,0b00110010,0b00001100,0b00000000,
};
static const uint8_t ALIEN_B2[ALIEN_W] = {
    0b00110000,0b00101000,0b00111100,0b00010100,0b00111100,
    0b00010100,0b00111100,0b00101000,0b00110000,0b00000000,
};

/* ─── state ──────────────────────────────────────────────────────────────── */
typedef struct { int x; int y; bool active; } Bullet;

static struct {
    bool     alive[ALIEN_ROWS][ALIEN_COLS];
    int      grid_x;
    int      grid_y;
    int      dir;
    int      anim_frame;

    int      player_x;
    int      dx_intent;

    Bullet   pbullet;
    Bullet   abullets[MAX_ALIEN_BULLETS];

    int      lives;
    uint32_t score;
    bool     game_over;

    int64_t  last_tick_ms;
    int64_t  last_march_ms;
    int      march_interval;

    bool     btn_last;
} SI;

static inline int64_t si_now(void) { return esp_timer_get_time() / 1000; }

#define GRID_WIDTH_PX   (ALIEN_COLS * CELL_W - ALIEN_GAP_X)

static int count_alive(void)
{
    int n = 0;
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++)
            if (SI.alive[r][c]) n++;
    return n;
}

static void alien_bounds(int row, int col, int *px, int *py)
{
    *px = SI.grid_x + col * CELL_W;
    *py = SI.grid_y + row * CELL_H;
}

static void alien_fire(void)
{
    int shooters[ALIEN_ROWS * ALIEN_COLS], n = 0;
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++)
            if (SI.alive[r][c]) shooters[n++] = r * ALIEN_COLS + c;

    if (n == 0) return;

    int slot = -1;
    for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (!SI.abullets[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    int pick = (int)(esp_random() % (uint32_t)n);
    int row  = shooters[pick] / ALIEN_COLS;
    int col  = shooters[pick] % ALIEN_COLS;

    int px, py;
    alien_bounds(row, col, &px, &py);
    SI.abullets[slot].x      = px + ALIEN_W / 2;
    SI.abullets[slot].y      = py + ALIEN_H;
    SI.abullets[slot].active = true;
}

void spaceinvaders_init(void)
{
    memset(&SI, 0, sizeof(SI));
    for (int r = 0; r < ALIEN_ROWS; r++)
        for (int c = 0; c < ALIEN_COLS; c++)
            SI.alive[r][c] = true;

    SI.grid_x         = ALIEN_START_X;
    SI.grid_y         = ALIEN_START_Y;
    SI.dir            = 1;
    SI.player_x       = (TFT_WIDTH - PLAYER_W) / 2;
    SI.lives          = 3;
    SI.march_interval = MARCH_INTERVAL;
    SI.last_tick_ms   = si_now();
    SI.last_march_ms  = si_now();
}

void spaceinvaders_input(int dx, int dy, bool btn)
{
    (void)dy;
    SI.dx_intent = dx;
    if (btn && !SI.btn_last && !SI.pbullet.active) {
        SI.pbullet.x      = SI.player_x + PLAYER_W / 2;
        SI.pbullet.y      = PLAYER_Y - BULLET_H;
        SI.pbullet.active = true;
        sound_play(NOTE_C5, 30);
    }
    SI.btn_last = btn;
}

bool spaceinvaders_tick(uint32_t *score_out)
{
    if (SI.game_over) { *score_out = SI.score; return false; }

    int64_t now = si_now();
    if ((now - SI.last_tick_ms) < TICK_MS) { *score_out = SI.score; return true; }
    SI.last_tick_ms = now;

    SI.player_x += SI.dx_intent * 3;
    if (SI.player_x < 1)                       SI.player_x = 1;
    if (SI.player_x > TFT_WIDTH - PLAYER_W - 1) SI.player_x = TFT_WIDTH - PLAYER_W - 1;

    if (SI.pbullet.active) {
        SI.pbullet.y -= PLAYER_BULLET_SPEED;
        if (SI.pbullet.y < FIELD_TOP) SI.pbullet.active = false;
    }

    for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (!SI.abullets[i].active) continue;
        SI.abullets[i].y += ALIEN_BULLET_SPEED;
        if (SI.abullets[i].y > TFT_HEIGHT) {
            SI.abullets[i].active = false;
            continue;
        }
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
            sound_play(NOTE_D4, 200);
        }
    }

    if ((now - SI.last_march_ms) >= SI.march_interval) {
        SI.last_march_ms = now;
        SI.anim_frame    = !SI.anim_frame;

        SI.grid_x += SI.dir * ALIEN_STEP_X;

        int left  = SI.grid_x;
        int right = SI.grid_x + GRID_WIDTH_PX;

        if (right >= TFT_WIDTH - 1 || left <= 1) {
            SI.dir     = -SI.dir;
            SI.grid_y += ALIEN_STEP_Y;

            int alive = count_alive();
            SI.march_interval = MARCH_INTERVAL * alive / (ALIEN_ROWS * ALIEN_COLS);
            if (SI.march_interval < 80) SI.march_interval = 80;
        }

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

        if ((int)(esp_random() % ALIEN_FIRE_ODDS) == 0) alien_fire();
    }

    if (SI.pbullet.active) {
        for (int r = 0; r < ALIEN_ROWS; r++) {
            for (int c = 0; c < ALIEN_COLS; c++) {
                if (!SI.alive[r][c]) continue;
                int px, py; alien_bounds(r, c, &px, &py);
                if (SI.pbullet.x >= px && SI.pbullet.x <= px + ALIEN_W &&
                    SI.pbullet.y >= py && SI.pbullet.y <= py + ALIEN_H) {
                    SI.alive[r][c]    = false;
                    SI.pbullet.active = false;

                    uint32_t pts = (r < 2) ? SCORE_TOP_ROW : (r < 4) ? SCORE_MID_ROW : SCORE_BOT_ROW;
                    SI.score += pts;

                    sound_play(r < 2 ? NOTE_G5 : (r < 4 ? NOTE_E5 : NOTE_C5), 35);

                    if (count_alive() == 0) {
                        for (int rr = 0; rr < ALIEN_ROWS; rr++)
                            for (int cc = 0; cc < ALIEN_COLS; cc++)
                                SI.alive[rr][cc] = true;
                        SI.grid_x = ALIEN_START_X;
                        SI.grid_y = ALIEN_START_Y;
                        SI.march_interval = MARCH_INTERVAL * 3 / 4;

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
    tft_fill_rect(0, HUD_H, TFT_WIDTH, TFT_HEIGHT - HUD_H, COL_BG);

    /* HUD — score on the left, heart sprites for lives on the right,
     * matching the reference image's heart-icon lives display instead
     * of a plain "LIVES:%d" text readout. */
    tft_fill_rect(0, 0, TFT_WIDTH, HUD_H, COL_BG);
    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE:%lu", (unsigned long)SI.score);
    font_draw_str(2, 4, buf, COL_HUD_TEXT);

    {
        int heart_x = TFT_WIDTH - 9;
        for (int i = 0; i < 3; i++) {
            bool filled = (i < SI.lives);
            effects_draw_heart(heart_x, 4, filled, COL_HEART);
            heart_x -= 9;
        }
    }

    for (int x = 0; x < TFT_WIDTH; x++) tft_draw_pixel(x, HUD_H - 1, COL_HUD_LINE);

    /* Ground line */
    for (int x = 0; x < TFT_WIDTH; x++) tft_draw_pixel(x, FIELD_BOT, COL_GROUND);

    /* Aliens — colored by row */
    for (int r = 0; r < ALIEN_ROWS; r++) {
        const uint8_t *sprite;
        if (r % 2 == 0) sprite = SI.anim_frame ? ALIEN_A2 : ALIEN_A;
        else            sprite = SI.anim_frame ? ALIEN_B2 : ALIEN_B;

        uint16_t color = ALIEN_ROW_COLORS[r];

        for (int c = 0; c < ALIEN_COLS; c++) {
            if (!SI.alive[r][c]) continue;
            int px, py; alien_bounds(r, c, &px, &py);
            for (int col = 0; col < ALIEN_W; col++) {
                uint8_t bits = sprite[col];
                for (int row = 0; row < ALIEN_H; row++)
                    if (bits & (1 << row)) tft_draw_pixel(px + col, py + row, color);
            }
        }
    }

    /* Player cannon — cyan triangle-ish shape */
    tft_fill_rect(SI.player_x, PLAYER_Y + PLAYER_H - 2, PLAYER_W, 2, COL_PLAYER);
    tft_fill_rect(SI.player_x + 2, PLAYER_Y + PLAYER_H - 4, PLAYER_W - 4, 2, COL_PLAYER);
    tft_fill_rect(SI.player_x + PLAYER_W/2 - 1, PLAYER_Y, 2, PLAYER_H - 4, COL_PLAYER);

    /* Player bullet */
    if (SI.pbullet.active) {
        tft_fill_rect(SI.pbullet.x, SI.pbullet.y, 2, BULLET_H, COL_PBULLET);
    }

    /* Alien bullets */
    for (int i = 0; i < MAX_ALIEN_BULLETS; i++) {
        if (!SI.abullets[i].active) continue;
        tft_fill_rect(SI.abullets[i].x, SI.abullets[i].y, 2, BULLET_H, COL_ABULLET);
    }
}