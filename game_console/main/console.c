/*
 * CONSOLE — top-level game selector and dispatcher. 128x160 color version.
 *
 * Responsibilities (unchanged from the monochrome version):
 *   - Animated game-select carousel
 *   - Per-game high score display (loaded from NVS on boot)
 *   - Countdown before launch
 *   - Pause menu (triggered by dedicated GPIO26 pushbutton)
 *   - Game-over screen with RETRY / MAIN MENU selector
 *   - NVS high score save on every game-over
 *
 * WHAT CHANGED FOR THE TFT MIGRATION:
 *   - Embedded font table replaced with the shared font.h/font.c module
 *   - Every oled_draw_pixel/oled_clear/oled_update call replaced with the
 *     equivalent tft_* call, now requiring a color argument
 *   - All y-coordinates recalculated for 160px height instead of 64px —
 *     this was a from-scratch layout pass, not a naive scale-up, since a
 *     literal 2.5x stretch would leave huge dead space between elements
 *   - Pause overlay can now use a REAL semi-dark tinted box instead of
 *     the monochrome version's "skip drawing the game" workaround — see
 *     draw_pause_overlay() for details
 *
 * Adding a new game — only change needed in this file:
 *   1. #include "my_game.h"
 *   2. Add one GameDesc literal to s_games[]
 */

#include "console.h"
#include "tft.h"
#include "font.h"
#include "palette.h"
#include "effects.h"
#include "nvs_scores.h"
#include "sound.h"
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
   COLOR PALETTE
   ══════════════════════════════════════════════════════════════════════════ */

#define COL_BG          PAL_BACKGROUND
#define COL_BORDER      PAL_BORDER
#define COL_TITLE       PAL_TITLE
#define COL_TEXT        PAL_TEXT
#define COL_DIM_TEXT    PAL_TEXT_DIM
#define COL_HILIGHT     PAL_SELECTED
#define COL_LINE        PAL_BORDER
#define COL_CURSOR      PAL_CURSOR
#define COL_COUNTDOWN   PAL_GOLD
#define COL_BEST        PAL_GOLD
#define COL_GAMEOVER    PAL_DANGER
#define COL_NEWBEST     PAL_GOLD
/*
 * Pause box now pulls from PAL_BG_PANEL — the palette's own dedicated
 * panel color — instead of a one-off hardcoded gray. Independently
 * verified (see conversation history) to give even stronger luminance
 * contrast against white text than the previous fixed value, while
 * additionally keeping the pause overlay visually consistent with every
 * other panel/menu surface across the console now that they all share
 * one palette.
 */
#define COL_PAUSE_BOX    PAL_PANEL_BG
#define COL_PAUSE_BORDER PAL_BLUE_MAIN
#define COL_PAUSE_TITLE  PAL_GOLD   /* gold still pops clearly against the dark panel */

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
    {
        "SNAKE", snake_w_init, snake_w_input, snake_w_tick, snake_w_draw,
        { "JOYSTICK: MOVE", "EAT APPLES TO GROW", "AVOID WALLS & SELF", NULL }
    },
    {
        "PONG", pong_init, pong_input, pong_tick, pong_draw,
        { "LEFT/RIGHT: PADDLE", "BOUNCE BALL UP", "DON'T MISS IT", NULL }
    },
    {
        "BREAKOUT", breakout_init, breakout_input, breakout_tick, breakout_draw,
        { "LEFT/RIGHT: PADDLE", "BREAK ALL BRICKS", "3 LIVES TOTAL", NULL }
    },
    {
        "FLAPPY", flappy_init, flappy_input, flappy_tick, flappy_draw,
        { "ACTION BTN: FLAP", "CLEAR THE PIPES", "AVOID GROUND/TOP", NULL }
    },
    {
        "INVADERS", spaceinvaders_init, spaceinvaders_input,
        spaceinvaders_tick, spaceinvaders_draw,
        { "LEFT/RIGHT: MOVE", "ACTION BTN: SHOOT", "STOP THEM LANDING", NULL }
    },
    {
        "MAZE", maze_init, maze_input, maze_tick, maze_draw,
        { "JOYSTICK: WALK", "COLLECT COINS", "PAUSE BTN: MENU", "TO EXIT MAZE" }
    },
};

#define GAME_COUNT  ((int)(sizeof(s_games) / sizeof(s_games[0])))

/* ══════════════════════════════════════════════════════════════════════════
   CONSOLE STATE MACHINE
   ══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    CON_SELECTING,
    CON_COUNTDOWN,
    CON_PLAYING,
    CON_PAUSED,
    CON_HELP,
    CON_GAME_OVER,
} ConState;

typedef enum {
    GO_RETRY = 0,
    GO_MENU,
    GO_ITEM_COUNT
} GoItem;

typedef enum {
    PAUSE_RESUME = 0,
    PAUSE_HELP,
    PAUSE_QUIT,
    PAUSE_ITEM_COUNT
} PauseItem;

/* ── state variables ──────────────────────────────────────────────────────── */

static ConState  s_state       = CON_SELECTING;
static int       s_sel         = 0;
static uint32_t  s_hi[GAME_COUNT];
static uint32_t  s_last_score  = 0;
static GoItem    s_go_sel      = GO_RETRY;
static PauseItem s_pause_sel   = PAUSE_RESUME;
static int       s_countdown   = 3;
static int64_t   s_cd_tick     = 0;

static bool    s_blink      = true;
static int64_t s_blink_tick = 0;

static int  s_last_dy  = 0;
static bool s_last_menu_btn = false;

static const char *s_nvs_keys[GAME_COUNT];

static inline int64_t con_now(void) { return esp_timer_get_time() / 1000; }

/* ══════════════════════════════════════════════════════════════════════════
   DRAW HELPERS
   ══════════════════════════════════════════════════════════════════════════ */

static void draw_border(void)
{
    tft_draw_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COL_BORDER);
}

/*
 * Centred vertical selector used by pause/help/game-over screens.
 * items[]   — label strings
 * count     — number of items
 * sel       — currently highlighted index
 * y_start   — y pixel of first item
 * row_h     — pixels per row
 */
/*
 * Selector redesign for clarity (reported as "not user friendly" — see
 * the pause menu screenshot where RESUME/HELP/QUIT all looked like
 * similar pale cyan text with no clear indication of which was selected).
 *
 * Previous version only changed TEXT COLOR for the selected item
 * (COL_TEXT white vs COL_HILIGHT bright cyan) — both are light colors on
 * a dark background, so the contrast between "selected" and "not
 * selected" was far weaker than the contrast between either of them and
 * the background. A person glancing at the screen sees "all the text is
 * roughly similarly bright" rather than "one item clearly stands out."
 *
 * Fixed by adding an actual highlight BAR behind the selected item (a
 * filled rectangle in the palette's main blue) in addition to the
 * brighter text color — now selection is communicated by a background
 * shape change, not just a text-color shift, which reads unambiguously
 * even at a glance.
 */
static void draw_selector(const char **items, int count, int sel,
                           int y_start, int row_h)
{
    int max_len = 0;
    for (int i = 0; i < count; i++) {
        int l = 0; for (const char *p = items[i]; *p; p++) l++;
        if (l > max_len) max_len = l;
    }
    int block_w = (2 + max_len) * 6;
    int block_x = (TFT_WIDTH - block_w) / 2;
    if (block_x < 0) block_x = 0;
    int label_x = block_x + 12;

    for (int i = 0; i < count; i++) {
        int y = y_start + i * row_h;
        bool is_sel = (i == sel);

        if (is_sel) {
            /* Highlight bar behind the selected row — main palette blue,
             * filling almost the full row height for a clear "selected"
             * block rather than relying on text color alone. */
            tft_fill_rect(block_x - 2, y - 2, block_w + 4, row_h - 2, PAL_BLUE_MAIN);
            font_draw_str(label_x, y, items[i], PAL_WHITE);
        } else {
            font_draw_str(label_x, y, items[i], COL_DIM_TEXT);
        }

        if (is_sel && s_blink) font_draw_char(block_x - 10, y, '>', PAL_BLUE_BRIGHT);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
   SCREEN: GAME SELECT CAROUSEL
   ────────────────────────────────────────────────────────────────────────── *
 * LAYOUT REDESIGNED FOR 160px HEIGHT (was 64px):
 *   y=  4   "SELECT GAME" header
 *   y= 16   separator
 *   y= 30   previous game (dim)
 *   y= 64   [ CURRENT GAME ] — large, highlighted, with bracket decoration
 *   y= 98   next game (dim)
 *   y=128   separator
 *
 * LAYOUT REDESIGNED FOR LANDSCAPE 160x128 (was portrait 128x160):
 *   y=  3   "SELECT GAME" header
 *   y= 14   separator
 *   y= 24   previous game (dim)
 *   y= 50   [ CURRENT GAME ] — large, highlighted
 *   y= 78   next game (dim)
 *   y= 96   separator
 *   y=102   "BEST: nnnn"
 *   y=116   "PRESS TO START"
 * ────────────────────────────────────────────────────────────────────────── */
static void draw_select_screen(void)
{
    draw_border();

    font_draw_str_centred(3, "SELECT GAME", COL_TITLE, TFT_WIDTH);
    for (int x = 1; x < TFT_WIDTH-1; x++) tft_draw_pixel(x, 14, COL_LINE);

    int prev_i = (s_sel - 1 + GAME_COUNT) % GAME_COUNT;
    int next_i = (s_sel + 1)              % GAME_COUNT;

    font_draw_str_centred(24, s_games[prev_i].name, COL_DIM_TEXT, TFT_WIDTH);

    /*
     * Current — large 2x text with bracket decoration.
     *
     * BUG FIX: the width calculation previously assumed "[ " and " ]"
     * (4 characters total, including spaces) but only "[" and "]" (2
     * characters, no spaces) were ever actually drawn — name_w + 4*11
     * overestimated the total width by 2*11=22px, which shifted the
     * whole bracketed block 11px left of true centre every time. Fixed
     * by using the ACTUAL character count drawn: name_w + 2*11.
     */
    {
        const char *name = s_games[s_sel].name;
        int len = 0; for (const char *p = name; *p; p++) len++;
        int name_w  = len * 11;          /* 2x font: 11px per char        */
        int total_w = name_w + 2 * 11;   /* "[" + name + "]" — 2 brackets */
        int bx      = (TFT_WIDTH - total_w) / 2;
        if (bx < 0) bx = 0;
        font_draw_str_2x(bx,             50, "[", COL_HILIGHT);
        font_draw_str_2x(bx + 11,        50, name, COL_HILIGHT);
        font_draw_str_2x(bx + 11 + name_w, 50, "]", COL_HILIGHT);
    }

    font_draw_str_centred(78, s_games[next_i].name, COL_DIM_TEXT, TFT_WIDTH);

    if (s_blink) {
        font_draw_char(76, 16, '^', COL_TITLE);
        font_draw_char(76, 88, 'v', COL_TITLE);
    }

    for (int x = 1; x < TFT_WIDTH-1; x++) tft_draw_pixel(x, 96, COL_LINE);

    char buf[24];
    snprintf(buf, sizeof(buf), "BEST: %lu", (unsigned long)s_hi[s_sel]);
    font_draw_str_centred(102, buf, COL_BEST, TFT_WIDTH);

    if (s_blink) {
        font_draw_str_centred(116, "PRESS TO START", COL_TEXT, TFT_WIDTH);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
   SCREEN: COUNTDOWN
   ────────────────────────────────────────────────────────────────────────── */
static void draw_countdown(void)
{
    draw_border();
    font_draw_str_centred(20, "GET READY", COL_TITLE, TFT_WIDTH);
    for (int x = 1; x < TFT_WIDTH-1; x++) tft_draw_pixel(x, 34, COL_LINE);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", s_countdown);
    /* Large 2x digit, centred */
    font_draw_str_2x_centred(60, buf, COL_COUNTDOWN, TFT_WIDTH);

    font_draw_str_centred(130, s_games[s_sel].name, COL_TEXT, TFT_WIDTH);
}

/* ──────────────────────────────────────────────────────────────────────────
   SCREEN: PAUSE OVERLAY
   ────────────────────────────────────────────────────────────────────────── *
 * COLOR UPGRADE FROM MONOCHROME:
 *
 * The 1-bit version had to SKIP drawing the underlying game entirely
 * while paused (a workaround documented in the previous version's
 * comments — Maze's fog-of-war fill made an outline-only box unreadable
 * since there's no way to "darken" a region on a 1-bit display).
 *
 * On color, we can do this properly: draw the frozen game frame, THEN
 * overlay a solid dark box on top (COL_PAUSE_BOX, a dark navy/gray that
 * reads as "dimmed" against any background) with a bright cyan border.
 * This is the real "semi-transparent overlay" look pause menus normally
 * have — genuinely impossible before, straightforward now.
 * ────────────────────────────────────────────────────────────────────────── */
static void draw_pause_overlay(void)
{
    /* Draw the frozen game frame underneath first */
    s_games[s_sel].draw();

    /*
     * Dark box overlay, centred on the new 160x128 landscape screen.
     * Previous coordinates (bx0=14,by0=40,bx1=113,by1=120) were sized
     * for the old 128x160 PORTRAIT screen — by1=120 left almost no
     * margin against the new 128px height, and the box wasn't centred
     * for the new 160px width either. Recalculated from scratch:
     */
    int box_w = 130, box_h = 80;
    int bx0 = (TFT_WIDTH  - box_w) / 2;
    int by0 = (TFT_HEIGHT - box_h) / 2;
    int bx1 = bx0 + box_w;
    int by1 = by0 + box_h;

    tft_fill_rect(bx0, by0, box_w, box_h, COL_PAUSE_BOX);
    tft_draw_rect(bx0, by0, box_w, box_h, COL_PAUSE_BORDER);

    /*
     * Text centring fixed to use the BOX width, not the full screen
     * width — font_draw_str_centred(y, text, color, width) centres
     * within [0, width), so passing TFT_WIDTH (the whole screen) while
     * the box itself is only 130px wide and offset from x=0 produced
     * text that was centred on the wrong reference frame. The cleanest
     * fix without changing font_draw_str_centred's signature is to
     * temporarily treat the box's own width as the centring reference
     * and offset the result by bx0.
     */
    {
        const char *title = "-- PAUSED --";
        int len = 0; for (const char *p = title; *p; p++) len++;
        int tw = len * 6;
        int tx = bx0 + (box_w - tw) / 2;
        font_draw_str(tx, by0 + 8, title, COL_PAUSE_TITLE);
    }

    const char *p_items[] = { "RESUME", "HELP", "QUIT" };
    draw_selector(p_items, PAUSE_ITEM_COUNT, (int)s_pause_sel, by0 + 26, 16);

    (void)bx1; (void)by1;
}

/* ──────────────────────────────────────────────────────────────────────────
   SCREEN: HELP
   ────────────────────────────────────────────────────────────────────────── */
/*
 * Layout fix: the bottom divider (was y=138) and "BTN: BACK" hint (was
 * y=146) were both being drawn BEYOND TFT_HEIGHT(128) — leftover from
 * the old 160px-tall portrait layout, never updated after the landscape
 * rotation. tft_draw_pixel()'s bounds check silently dropped every pixel
 * of both, so this footer has been invisible since the rotation fix.
 * Repositioned to fit inside the actual 128px height, with help lines
 * given a touch more breathing room (18px instead of 16px) since there's
 * now a clearly-bounded footer area to leave room for.
 *
 * Hint text also clarified from "BTN: BACK" to "JOYSTICK BTN: BACK" —
 * the console now has three distinct physical buttons (joystick click,
 * GPIO26 pause, GPIO27 action), so a bare "BTN" no longer unambiguously
 * identifies which one returns from this screen.
 */
/*
 * Help screen redesigned with an actual dark panel box around the
 * content (matching the pause overlay's now-established pattern)
 * instead of help text floating directly on the bare background with
 * no visual structure.
 */
static void draw_help_screen(void)
{
    draw_border();

    font_draw_str_centred(4, s_games[s_sel].name, COL_TITLE, TFT_WIDTH);
    for (int x = 1; x < TFT_WIDTH-1; x++) tft_draw_pixel(x, 18, COL_LINE);

    /* Dark panel box for the help text itself */
    int bx0 = 10, by0 = 24, box_w = TFT_WIDTH - 20, box_h = 80;
    tft_fill_rect(bx0, by0, box_w, box_h, PAL_PANEL_BG);
    tft_draw_rect(bx0, by0, box_w, box_h, PAL_BLUE_MAIN);

    int y = by0 + 8;
    for (int i = 0; i < 4 && s_games[s_sel].help[i] != NULL; i++) {
        font_draw_str_centred(y, s_games[s_sel].help[i], PAL_WHITE, TFT_WIDTH);
        y += 18;
    }

    for (int x = 1; x < TFT_WIDTH-1; x++) tft_draw_pixel(x, 110, COL_LINE);
    if (s_blink) font_draw_str_centred(116, "JOYSTICK BTN: BACK", COL_DIM_TEXT, TFT_WIDTH);
}

/* ──────────────────────────────────────────────────────────────────────────
   SCREEN: GAME OVER
   ────────────────────────────────────────────────────────────────────────── */
/*
 * Layout redesigned for the landscape 128px height (TFT_HEIGHT).
 *
 * Previous y-coordinates (10, 32, 42, 58, 78, 96) were tuned for the
 * earlier 160px-tall portrait screen and never updated after the
 * landscape rotation. Two visible problems resulted:
 *   1. An 18px dead gap between the second divider (y=78) and the first
 *      selector item (y=96) — the "white space after the divider".
 *   2. The selector's last row landed only 3px above the bottom border —
 *      "cutting too close to the bottom".
 *
 * Redesigned from scratch for 128px height: every element is placed
 * immediately after the one before it with a small intentional gap
 * (6px after dividers, matching the spacing used elsewhere), and the
 * selector block now ends with a comfortable ~39px margin from the
 * bottom border instead of 3px.
 */
/*
 * Redesigned to use the full 128px screen height instead of ending
 * around y=94 with ~26px of unused space below. Changes:
 *   - Score and best/new-best now both shown (previously only one or
 *     the other, depending on whether this run beat the record)
 *   - Selector items now sit inside a dark panel box matching the help
 *     screen's and pause overlay's visual language, rather than floating
 *     on the bare background
 *   - Ambient twinkling stars drawn behind everything for visual
 *     consistency with the main menu, instead of a flat empty background
 */
static void draw_game_over(void)
{
    effects_stars_draw();
    draw_border();

    font_draw_str_2x_centred(4, "GAME OVER", COL_GAMEOVER, TFT_WIDTH);
    for (int x = 1; x < TFT_WIDTH-1; x++) tft_draw_pixel(x, 24, COL_LINE);

    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE: %lu", (unsigned long)s_last_score);
    font_draw_str_centred(30, buf, COL_TEXT, TFT_WIDTH);

    bool new_best = (s_last_score > 0 && s_last_score >= s_hi[s_sel]);
    if (new_best) {
        font_draw_str_centred(42, "** NEW BEST **", COL_NEWBEST, TFT_WIDTH);
    } else {
        snprintf(buf, sizeof(buf), "BEST: %lu", (unsigned long)s_hi[s_sel]);
        font_draw_str_centred(42, buf, COL_BEST, TFT_WIDTH);
    }

    for (int x = 1; x < TFT_WIDTH-1; x++) tft_draw_pixel(x, 56, COL_LINE);

    /* Selector panel — fills the remaining vertical space down to a
     * comfortable margin above the bottom border */
    int panel_y = 62, panel_h = 56;
    tft_fill_rect(8, panel_y, TFT_WIDTH - 16, panel_h, PAL_PANEL_BG);
    tft_draw_rect(8, panel_y, TFT_WIDTH - 16, panel_h, PAL_BLUE_MAIN);

    const char *go_items[] = { "RETRY", "MAIN MENU" };
    draw_selector(go_items, GO_ITEM_COUNT, (int)s_go_sel, panel_y + 10, 18);
}

/* ══════════════════════════════════════════════════════════════════════════
   PUBLIC API
   ══════════════════════════════════════════════════════════════════════════ */

void console_init(void)
{
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

    s_state       = CON_SELECTING;
    s_sel         = 0;
    s_last_score  = 0;
    s_go_sel      = GO_RETRY;
    s_pause_sel   = PAUSE_RESUME;
    s_countdown   = 3;
    s_cd_tick     = con_now();
    s_blink       = true;
    s_blink_tick  = con_now();
    s_last_dy     = 0;

    /* Ambient twinkling starfield for menu screens — purely decorative,
     * matches the reference image's starry background motif. */
    effects_stars_init(TFT_WIDTH, TFT_HEIGHT);
    s_last_menu_btn = true;
}

void console_input(int dx, int dy, bool menu_btn, bool action_btn)
{
    int64_t now = con_now();

    if ((now - s_blink_tick) >= 500) {
        s_blink      = !s_blink;
        s_blink_tick = now;
    }

    bool menu_btn_pressed = (menu_btn && !s_last_menu_btn);
    bool dy_new           = (dy != 0 && s_last_dy == 0);
    s_last_dy  = dy;

    s_last_menu_btn = menu_btn;

    switch (s_state) {

    case CON_SELECTING:
        if (dy_new) {
            s_sel = (s_sel + dy + GAME_COUNT) % GAME_COUNT;
            sound_play(NOTE_C4, 25);
        }
        if (menu_btn_pressed) {
            s_countdown = 3;
            s_cd_tick   = now;
            s_state     = CON_COUNTDOWN;
            sound_play(NOTE_G4, 60);
        }
        break;

    case CON_COUNTDOWN:
        break;

    case CON_PLAYING:
        s_games[s_sel].input(dx, dy, action_btn);
        break;

    case CON_PAUSED:
        if (dy_new) {
            int n = ((int)s_pause_sel + dy + PAUSE_ITEM_COUNT) % PAUSE_ITEM_COUNT;
            s_pause_sel = (PauseItem)n;
            sound_play(NOTE_C4, 25);
        }
        if (menu_btn_pressed) {
            if (s_pause_sel == PAUSE_RESUME) {
                s_state = CON_PLAYING;
                sound_play(NOTE_E4, 60);
            } else if (s_pause_sel == PAUSE_HELP) {
                s_state = CON_HELP;
                sound_play(NOTE_G4, 50);
            } else {
                s_state = CON_SELECTING;
                sound_play(NOTE_C4, 80);
            }
        }
        break;

    case CON_HELP:
        if (menu_btn_pressed) {
            s_state = CON_PAUSED;
            sound_play(NOTE_C4, 40);
        }
        break;

    case CON_GAME_OVER:
        if (dy_new) {
            int n = ((int)s_go_sel + dy + GO_ITEM_COUNT) % GO_ITEM_COUNT;
            s_go_sel = (GoItem)n;
            sound_play(NOTE_C4, 25);
        }
        if (menu_btn_pressed) {
            if (s_go_sel == GO_RETRY) {
                s_countdown = 3;
                s_cd_tick   = now;
                s_state     = CON_COUNTDOWN;
                sound_play(NOTE_G4, 60);
            } else {
                s_state = CON_SELECTING;
                sound_play(NOTE_C4, 80);
            }
        }
        break;
    }
}

void console_pause_request(void)
{
    if (s_state == CON_PLAYING) {
        sound_play(NOTE_E4, 50);
        s_state     = CON_PAUSED;
        s_pause_sel = PAUSE_RESUME;
    }
}

void console_tick(void)
{
    int64_t now = con_now();

    switch (s_state) {

    case CON_SELECTING:
        tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COL_BG);
        effects_stars_draw();   /* ambient starfield, drawn behind menu content */
        draw_select_screen();
        break;

    case CON_COUNTDOWN:
        if ((now - s_cd_tick) >= 1000) {
            s_countdown--;
            s_cd_tick = now;

            if (s_countdown <= 0) {
                s_games[s_sel].init();
                s_state = CON_PLAYING;
                static const Note launch_tune[] = { { NOTE_C5, 60 }, { NOTE_E5, 90 } };
                sound_play_melody(launch_tune, sizeof(launch_tune)/sizeof(launch_tune[0]));
                break;
            }
            sound_play(NOTE_A4, 50);
        }
        tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COL_BG);
        draw_countdown();
        break;

    case CON_PLAYING: {
        uint32_t sc   = 0;
        bool     alive = s_games[s_sel].tick(&sc);
        s_games[s_sel].draw();
        if (!alive) {
            s_last_score = sc;
            bool new_best = (sc > s_hi[s_sel]);

            if (new_best) {
                s_hi[s_sel] = sc;
                nvs_scores_set(s_nvs_keys[s_sel], sc);
            }

            if (new_best && sc > 0) {
                static const Note hiscore_tune[] = {
                    { NOTE_C5, 80 }, { NOTE_E5, 80 }, { NOTE_G5, 80 }, { NOTE_C6, 150 },
                };
                sound_play_melody(hiscore_tune, sizeof(hiscore_tune)/sizeof(hiscore_tune[0]));
            } else {
                static const Note gameover_tune[] = {
                    { NOTE_G4, 90 }, { NOTE_E4, 90 }, { NOTE_C4, 200 },
                };
                sound_play_melody(gameover_tune, sizeof(gameover_tune)/sizeof(gameover_tune[0]));
            }

            s_go_sel = GO_RETRY;
            s_state  = CON_GAME_OVER;
        }
        break;
    }

    case CON_PAUSED:
        /*
         * No longer needs to skip the game draw — draw_pause_overlay()
         * draws the frozen frame itself, then overlays a real dark box
         * on top (see the function's own comment for why this is now
         * possible on color where it wasn't on the 1-bit display).
         */
        draw_pause_overlay();
        break;

    case CON_HELP:
        tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COL_BG);
        draw_help_screen();
        break;

    case CON_GAME_OVER:
        tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COL_BG);
        draw_game_over();
        break;
    }

    tft_update();
}