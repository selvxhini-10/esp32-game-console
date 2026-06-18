/*
 * CONSOLE — top-level game selector and dispatcher.
 *
 * Responsibilities:
 *   - Animated game-select carousel (scrolls through all registered games)
 *   - Per-game high score display (loaded from NVS on boot)
 *   - Countdown before launch
 *   - Pause menu (hold button 1 s during play)
 *   - Game-over screen with RETRY / MAIN MENU selector
 *   - NVS high score save on every game-over
 *
 * Adding a new game — only change needed in this file:
 *   1. #include "my_game.h"
 *   2. Add one GameDesc literal to s_games[]
 */

#include "console.h"
#include "oled.h"
#include "nvs_scores.h"
#include "snake.h"
#include "pong.h"
#include "breakout.h"
#include "flappy.h"
#include "spaceinvaders.h"
#include "maze.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════════════
   FONT
   ══════════════════════════════════════════════════════════════════════════ */

static const uint8_t s_font[][5] = {
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

static void fchar(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *g = s_font[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t b = g[col];
        for (int row = 0; row < 8; row++)
            if (b & (1 << row)) oled_draw_pixel(x + col, y + row);
    }
}

static int fstr(int x, int y, const char *s)
{
    int cx = x;
    while (*s) { fchar(cx, y, *s++); cx += 6; }
    return cx - x;
}

static void fstr_c(int y, const char *s)    /* centred */
{
    int len = 0; for (const char *p = s; *p; p++) len++;
    int x = (128 - len * 6) / 2; if (x < 0) x = 0;
    fstr(x, y, s);
}

/* ══════════════════════════════════════════════════════════════════════════
   SNAKE WRAPPERS  (adapts SnakeGame to the GameDesc interface)
   ══════════════════════════════════════════════════════════════════════════ */

static SnakeGame s_snake;
static int64_t   s_snake_step_ts;

static void snake_w_init(void)
{
    snake_init(&s_snake);
    s_snake_step_ts = esp_timer_get_time() / 1000;
}

static void snake_w_input(int dx, int dy, bool btn)
{
    (void)btn;
    snake_input(&s_snake, dx, dy);
}

static bool snake_w_tick(uint32_t *sc)
{
    int64_t now = esp_timer_get_time() / 1000;
    if ((now - s_snake_step_ts) >= snake_get_step_ms(&s_snake)) {
        s_snake_step_ts = now;
        SnakeStatus st = snake_tick(&s_snake);
        if (st != SNAKE_ALIVE) { *sc = snake_get_score(&s_snake); return false; }
    }
    *sc = snake_get_score(&s_snake);
    return true;
}

static void snake_w_draw(void) { snake_draw(&s_snake); }

/* ══════════════════════════════════════════════════════════════════════════
   GAME REGISTRY  — add new games here only
   ══════════════════════════════════════════════════════════════════════════ */

static const GameDesc s_games[] = {
    { "SNAKE",    snake_w_init,   snake_w_input,  snake_w_tick,  snake_w_draw  },
    { "PONG",     pong_init,      pong_input,     pong_tick,     pong_draw     },
    { "BREAKOUT", breakout_init,  breakout_input, breakout_tick, breakout_draw },
    { "FLAPPY",   flappy_init,    flappy_input,   flappy_tick,   flappy_draw   },
    { "INVADERS",  spaceinvaders_init, spaceinvaders_input, spaceinvaders_tick, spaceinvaders_draw },
    { "MAZE",      maze_init,      maze_input,     maze_tick,     maze_draw     },
};

#define GAME_COUNT  ((int)(sizeof(s_games) / sizeof(s_games[0])))

/* ══════════════════════════════════════════════════════════════════════════
   CONSOLE STATE MACHINE
   ══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    CON_SELECTING,      /* scrolling game list            */
    CON_COUNTDOWN,      /* 3-2-1 before launch            */
    CON_PLAYING,        /* game running                   */
    CON_PAUSED,         /* pause overlay                  */
    CON_GAME_OVER,      /* score + RETRY / MENU selector  */
} ConState;

typedef enum {
    GO_RETRY = 0,
    GO_MENU,
    GO_ITEM_COUNT
} GoItem;

typedef enum {
    PAUSE_RESUME = 0,
    PAUSE_QUIT,
    PAUSE_ITEM_COUNT
} PauseItem;

/* ── state variables ──────────────────────────────────────────────────────── */

static ConState  s_state       = CON_SELECTING;
static int       s_sel         = 0;            /* selected game index        */
static uint32_t  s_hi[GAME_COUNT];            /* RAM high score cache       */
static uint32_t  s_last_score  = 0;
static GoItem    s_go_sel      = GO_RETRY;
static PauseItem s_pause_sel   = PAUSE_RESUME;
static int       s_countdown   = 3;
static int64_t   s_cd_tick     = 0;           /* ms timestamp per count     */
static int64_t   s_btn_held_ts = 0;           /* for pause hold detection   */
static bool      s_btn_was_held= false;

/* blink */
static bool    s_blink      = true;
static int64_t s_blink_tick = 0;

/* input debounce */
static int  s_last_dy  = 0;
static bool s_last_btn = false;

/* NVS keys per game (must be ≤15 chars each) */
static const char *s_nvs_keys[GAME_COUNT];   /* filled in console_init */

static inline int64_t con_now(void) { return esp_timer_get_time() / 1000; }

/* ══════════════════════════════════════════════════════════════════════════
   DRAW HELPERS
   ══════════════════════════════════════════════════════════════════════════ */

static void draw_hline(int y)
{
    for (int x = 0; x < 128; x++) oled_draw_pixel(x, y);
}

static void draw_border(void)
{
    draw_hline(0); draw_hline(63);
    for (int y = 0; y < 64; y++) { oled_draw_pixel(0,y); oled_draw_pixel(127,y); }
}

/*
 * Centred two-item vertical selector used by both game-over and pause screens.
 * items[]   — label strings
 * count     — number of items
 * sel       — currently highlighted index
 * y_start   — y pixel of first item
 * row_h     — pixels per row
 */
static void draw_selector(const char **items, int count, int sel,
                           int y_start, int row_h)
{
    /* Measure widest label so block is stable when cursor toggles */
    int max_len = 0;
    for (int i = 0; i < count; i++) {
        int l = 0; for (const char *p = items[i]; *p; p++) l++;
        if (l > max_len) max_len = l;
    }
    /* Total block: "> " (2 chars) + widest label */
    int block_w = (2 + max_len) * 6;
    int block_x = (128 - block_w) / 2;
    if (block_x < 0) block_x = 0;
    int label_x = block_x + 12;

    for (int i = 0; i < count; i++) {
        int y = y_start + i * row_h;
        if (i == sel && s_blink) fchar(block_x, y, '>');
        fstr(label_x, y, items[i]);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
   SCREEN: GAME SELECT CAROUSEL
   Visible window: 3 items.  Selected item is in the middle row with a
   solid inverted-look bracket: "[SNAKE]" drawn with pixel brackets.
   Items above/below are shown dimly (just text, no decoration).
   ────────────────────────────────────────────────────────────────────────── */
static void draw_select_screen(void)
{
    draw_border();

    /* Header */
    fstr_c(3, "SELECT GAME");
    draw_hline(12);

    /*
     * Carousel rows centred in the 50px between y=13 and y=63.
     * Row layout:
     *   prev (y=15): dim, small indent
     *   curr (y=27): highlighted with bracket decoration
     *   next (y=39): dim
     * High score sits at y=52.
     */
    int prev_i = (s_sel - 1 + GAME_COUNT) % GAME_COUNT;
    int next_i = (s_sel + 1)              % GAME_COUNT;

    /* Previous (greyed — just name, centred) */
    fstr_c(15, s_games[prev_i].name);

    /* Current — draw [ NAME ] brackets */
    {
        const char *name  = s_games[s_sel].name;
        int len = 0; for (const char *p = name; *p; p++) len++;
        int name_w  = len * 6;
        int total_w = name_w + 4 * 6;   /* "[ " + " ]" = 4 chars */
        int bx      = (128 - total_w) / 2;
        fstr(bx,          27, "[ ");
        fstr(bx + 2*6,    27, name);
        fstr(bx + 2*6 + name_w, 27, " ]");
    }

    /* Next */
    fstr_c(39, s_games[next_i].name);

    /* Scroll hint arrows */
    if (s_blink) {
        fchar(60, 14, '^');
        fchar(60, 48, 'v');
    }

    /* Separator + high score */
    draw_hline(51);
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "BEST: %lu", (unsigned long)s_hi[s_sel]);
        fstr_c(54, buf);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
   SCREEN: COUNTDOWN
   ────────────────────────────────────────────────────────────────────────── */
static void draw_countdown(void)
{
    draw_border();
    fstr_c(10, "GET READY");
    draw_hline(20);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", s_countdown);
    /* Draw large — 2× scale the single digit */
    int cx = (128 - 11) / 2;   /* 11px = one 2× char */
    char c = buf[0];
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *g = s_font[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t b = g[col];
        for (int row = 0; row < 8; row++) {
            if (b & (1 << row)) {
                oled_draw_pixel(cx + col*2,     28 + row*2);
                oled_draw_pixel(cx + col*2 + 1, 28 + row*2);
                oled_draw_pixel(cx + col*2,     28 + row*2 + 1);
                oled_draw_pixel(cx + col*2 + 1, 28 + row*2 + 1);
            }
        }
    }
    fstr_c(55, s_games[s_sel].name);
}

/* ──────────────────────────────────────────────────────────────────────────
   SCREEN: PAUSE OVERLAY (drawn on top of frozen game frame)
   ────────────────────────────────────────────────────────────────────────── */
static void draw_pause_overlay(void)
{
    /* Darken centre by drawing a filled rectangle (pixel overlay on 1-bit:
       we invert by simply drawing on all pixels in a band — the contrast
       forces a visible box even on monochrome) */
    for (int y = 18; y <= 50; y++)
        for (int x = 24; x <= 103; x++)
            oled_draw_pixel(x, y);

    /* Invert text region: draw background box edge */
    for (int x = 24; x <= 103; x++) {
        oled_draw_pixel(x, 18); oled_draw_pixel(x, 50);
    }
    for (int y = 18; y <= 50; y++) {
        oled_draw_pixel(24, y); oled_draw_pixel(103, y);
    }

    /* On 1-bit displays "dark box" isn't possible without clear_pixel.
       Instead draw the box outline only and put text inside (pixels set). */
    /* Clear inner area first by NOT drawing it.
       Workaround: the framebuffer was cleared before game_draw(), so
       we draw the border box and text into it — the inside stays dark. */

    /* Re-draw border box (overwrite the fill above) */
    /* Actually: clear the frame, let game_draw happen, then overlay text */
    /* This function is called AFTER oled_clear()+game_draw(), so the
       game frame is already in the buffer. We draw a solid rectangle on
       top to act as the overlay — on SH1106 "set" = white, so white box
       with white text isn't readable. Best approach: draw outline only + text. */

    /* The cleanest 1-bit approach: don't fill, just frame + text */
    fstr_c(23, "-- PAUSED --");
    const char *p_items[] = { "RESUME", "QUIT" };
    draw_selector(p_items, PAUSE_ITEM_COUNT, (int)s_pause_sel, 33, 12);
}

/* ──────────────────────────────────────────────────────────────────────────
   SCREEN: GAME OVER
   ────────────────────────────────────────────────────────────────────────── */
static void draw_game_over(void)
{
    /*
     * Fixed layout — every element has a guaranteed pixel slot so
     * MAIN MENU always lands within the 64px display height.
     *
     * y= 2   "GAME OVER" title
     * y=11   separator
     * y=14   SCORE: xxxx
     * y=24   "NEW BEST!" OR "BEST: xxxx"  (one line — mutually exclusive)
     * y=34   separator
     * y=38   > RETRY         (row 0)
     * y=52   > MAIN MENU     (row 1, row_h=14 → bottom pixel at y=59, inside border)
     */
    draw_border();

    fstr_c(2, "GAME OVER");
    draw_hline(11);

    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE:%lu", (unsigned long)s_last_score);
    fstr_c(14, buf);

    /* One info line only — new best takes priority over showing stored best */
    if (s_last_score > 0 && s_last_score >= s_hi[s_sel]) {
        fstr_c(24, "NEW BEST!");
    } else {
        snprintf(buf, sizeof(buf), "BEST:%lu", (unsigned long)s_hi[s_sel]);
        fstr_c(24, buf);
    }

    draw_hline(34);

    const char *go_items[] = { "RETRY", "MAIN MENU" };
    draw_selector(go_items, GO_ITEM_COUNT, (int)s_go_sel, 38, 14);
}

/* ══════════════════════════════════════════════════════════════════════════
   PUBLIC API
   ══════════════════════════════════════════════════════════════════════════ */

void console_init(void)
{
    /* NVS keys — must be ≤15 chars, unique */
    static const char *keys[GAME_COUNT];
    keys[0] = "hi_snake";
    keys[1] = "hi_pong";
    keys[2] = "hi_breakout";
    keys[3] = "hi_flappy";
    keys[4] = "hi_invaders";
    keys[5] = "hi_maze";
    for (int i = 0; i < GAME_COUNT; i++) {
        s_nvs_keys[i] = keys[i];
        s_hi[i]       = nvs_scores_get(keys[i]);
    }

    s_state       = CON_SELECTING;   /* always start at game selector, never countdown */
    s_sel         = 0;
    s_last_score  = 0;
    s_go_sel      = GO_RETRY;
    s_pause_sel   = PAUSE_RESUME;
    s_countdown   = 3;
    s_cd_tick     = con_now();        /* FIX: init to NOW so (now-s_cd_tick) starts at 0 */
    s_blink       = true;
    s_blink_tick  = con_now();
    s_last_dy     = 0;
    s_last_btn    = false;
    s_btn_was_held= false;
}

void console_input(int dx, int dy, bool btn)
{
    int64_t now = con_now();

    /* Blink */
    if ((now - s_blink_tick) >= 500) {
        s_blink      = !s_blink;
        s_blink_tick = now;
    }

    /* Edge detection */
    bool btn_pressed = (btn && !s_last_btn);
    bool dy_new      = (dy != 0 && s_last_dy == 0);
    s_last_dy  = dy;

    /* Pause hold: button held ≥1 s during play */
    if (s_state == CON_PLAYING) {
        if (btn) {
            if (!s_btn_was_held) { s_btn_held_ts = now; s_btn_was_held = true; }
            if ((now - s_btn_held_ts) >= 1000) {
                /* Trigger pause */
                s_state     = CON_PAUSED;
                s_pause_sel = PAUSE_RESUME;
                s_last_btn  = btn;
                return;
            }
        } else {
            s_btn_was_held = false;
        }
    }

    s_last_btn = btn;

    switch (s_state) {

    /* ── SELECTING ── */
    case CON_SELECTING:
        if (dy_new) {
            s_sel = (s_sel + dy + GAME_COUNT) % GAME_COUNT;
        }
        if (btn_pressed) {
            s_countdown = 3;
            s_cd_tick   = now;
            s_state     = CON_COUNTDOWN;
        }
        break;

    /* ── COUNTDOWN ── */
    case CON_COUNTDOWN:
        /* No input during countdown */
        break;

    /* ── PLAYING ── */
    case CON_PLAYING:
        s_games[s_sel].input(dx, dy, btn);
        break;

    /* ── PAUSED ── */
    case CON_PAUSED:
        if (dy_new) {
            int n = ((int)s_pause_sel + dy + PAUSE_ITEM_COUNT) % PAUSE_ITEM_COUNT;
            s_pause_sel = (PauseItem)n;
        }
        if (btn_pressed) {
            if (s_pause_sel == PAUSE_RESUME) {
                s_state        = CON_PLAYING;
                s_btn_was_held = false;
            } else {
                /* Quit to selector */
                s_state = CON_SELECTING;
            }
        }
        break;

    /* ── GAME OVER ── */
    case CON_GAME_OVER:
        if (dy_new) {
            int n = ((int)s_go_sel + dy + GO_ITEM_COUNT) % GO_ITEM_COUNT;
            s_go_sel = (GoItem)n;
        }
        if (btn_pressed) {
            if (s_go_sel == GO_RETRY) {
                s_countdown = 3;
                s_cd_tick   = now;
                s_state     = CON_COUNTDOWN;
            } else {
                s_state = CON_SELECTING;
            }
        }
        break;
    }
}

void console_tick(void)
{
    int64_t now = con_now();
    oled_clear();

    switch (s_state) {

    /* ── SELECTING ── */
    case CON_SELECTING:
        draw_select_screen();
        break;

    /* ── COUNTDOWN ── */
    case CON_COUNTDOWN:
        if ((now - s_cd_tick) >= 1000) {
            s_countdown--;
            s_cd_tick = now;
            if (s_countdown <= 0) {
                s_games[s_sel].init();
                s_state        = CON_PLAYING;
                s_btn_was_held = false;
                break;
            }
        }
        draw_countdown();
        break;

    /* ── PLAYING ── */
    case CON_PLAYING: {
        uint32_t sc   = 0;
        bool     alive = s_games[s_sel].tick(&sc);
        s_games[s_sel].draw();
        if (!alive) {
            s_last_score = sc;
            /* Update high score in RAM + NVS */
            if (sc > s_hi[s_sel]) {
                s_hi[s_sel] = sc;
                nvs_scores_set(s_nvs_keys[s_sel], sc);
            }
            s_go_sel = GO_RETRY;
            s_state  = CON_GAME_OVER;
        }
        break;
    }

    /* ── PAUSED ── */
    case CON_PAUSED:
        /* Draw frozen game underneath then overlay pause menu */
        s_games[s_sel].draw();
        draw_pause_overlay();
        break;

    /* ── GAME OVER ── */
    case CON_GAME_OVER:
        draw_game_over();
        break;
    }

    oled_update();
}