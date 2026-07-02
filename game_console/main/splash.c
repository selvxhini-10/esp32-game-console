/*
 * Boot splash — animated title screen. 160x128 landscape color version.
 *
 * WATCHDOG FIX:
 *   draw_flash() previously used nested tft_draw_pixel loops to fill a
 *   circle (up to 17x17 = 289 individual SPI pixel writes per frame).
 *   On ESP32, each tft_draw_pixel involves a CS/DC toggle + SPI byte
 *   sequence with no vTaskDelay — 289 of them back-to-back in the main
 *   task starved IDLE0 long enough to trigger the task watchdog.
 *   Fixed by replacing the pixel loop with a single tft_fill_rect call
 *   (one SPI burst for the whole rectangle regardless of size).
 *
 * TEXT OVERFLOW FIX:
 *   "ESP32 GAME CONSOLE" as a single 2x string = 18 * 11 - 1 = 197px,
 *   overflowing both sides of the 160px screen by ~18px each.
 *   Fixed: badge line is now "ESP32" only, drawn at 1x scale and centred.
 *   "GAME" and "CONSOLE" remain the main logo at 2x, which fit fine
 *   (7 chars * 11 - 1 = 76px and 4 chars * 11 - 1 = 43px respectively).
 *
 * Animation sequence:
 *   Phase 0   0– 200ms  Black hold — display settle
 *   Phase 1 200– 600ms  Starburst: cross-lines expand from centre +
 *                        a filled square flash (tft_fill_rect, no pixel loop)
 *   Phase 2 600–1100ms  "ESP32" typewriter reveal at 1x, centred
 *   Phase 3 1100–1500ms "GAME" / "CONSOLE" ease-out drop from above
 *   Phase 4 1500–2000ms Scanline sweep top-to-bottom
 *   Phase 5 2000ms+     Full logo hold, blinking "PRESS BTN", star field
 */

#include "splash.h"
#include "tft.h"
#include "font.h"
#include "splash_font.h"
#include "palette.h"
#include "effects.h"
#include "esp_timer.h"
#include <string.h>

/* ─── timing helpers ─────────────────────────────────────────────────────── */
static inline int64_t sp_now(void) { return esp_timer_get_time() / 1000; }

static int lerp(int a, int b, int64_t t, int64_t dur)
{
    if (t <= 0)   return a;
    if (t >= dur) return b;
    return (int)(a + (b - a) * t / dur);
}

/* Ease-out quad: decelerates into final position — logo "settle" feel. */
static int ease_out(int a, int b, int64_t t, int64_t dur)
{
    if (t <= 0)   return a;
    if (t >= dur) return b;
    int64_t p  = (dur - t) * 256 / dur;
    int64_t p2 = (p * p) >> 8;
    return (int)(b - ((b - a) * p2 >> 8));
}

/* ─── layout ─────────────────────────────────────────────────────────────── */
/*
 * Y positions designed for 160x128 landscape:
 *   Y_BADGE  — "ESP32" at 1x (7px tall), top of screen with margin
 *   Y_LOGO   — "GAME" at 2x (16px tall)
 *   Y_LOGO2  — "CONSOLE" at 2x, 22px below GAME (16px glyph + 6px gap)
 *   Y_HINT   — "PRESS BTN" at 1x, 5px above bottom border
 *
 * Separator line sits between LOGO2 and HINT.
 */
#define Y_BADGE    6
#define Y_LOGO    40
#define Y_LOGO2   62
#define Y_SEP     (Y_LOGO2 + 20)
#define Y_HINT    110

/* ─── colors ─────────────────────────────────────────────────────────────── */
#define COL_BG       PAL_BG_DARK
#define COL_BADGE    PAL_BLUE_BRIGHT   /* "ESP32" subtitle — cyan            */
#define COL_LOGO_A   PAL_BLUE_MAIN    /* "GAME"    — blue                   */
#define COL_LOGO_B   PAL_GOLD         /* "CONSOLE" — gold                   */
#define COL_HINT     PAL_WHITE
#define COL_SCANLINE PAL_BLUE_BRIGHT
#define COL_FLASH    PAL_WHITE

/* ─── phase timing (ms) ─────────────────────────────────────────────────── */
#define T_FLASH_START  200
#define T_FLASH_END    600
#define T_TYPE_START   600
#define T_TYPE_END    1100
#define T_DROP_START  1100
#define T_DROP_END    1500
#define T_SCAN_START  1500
#define T_SCAN_END    2000
#define T_HOLD_START  2000
#define T_TIMEOUT    15000

/* ─── state ──────────────────────────────────────────────────────────────── */
static int64_t s_start_ms   = 0;
static bool    s_blink      = false;
static int64_t s_blink_tick = 0;
static bool    s_stars_init = false;

/* ─── phase 1: starburst flash ───────────────────────────────────────────── *
 * Cross-lines extend from centre + a filled square flash.
 * Uses tft_draw_line and tft_fill_rect ONLY — no per-pixel loops,
 * so the SPI burst stays bounded and the watchdog cannot fire here.
 *
 * First half (expand): lines grow outward, centre square grows.
 * Second half (contract): lines stay, centre square shrinks to nothing.
 */
static void draw_flash(int64_t t)
{
    int64_t dur = T_FLASH_END - T_FLASH_START;
    int64_t dt  = t - T_FLASH_START;
    if (dt < 0 || dt > dur) return;

    int cx = TFT_WIDTH  / 2;
    int cy = TFT_HEIGHT / 2;

    if (dt < dur / 2) {
        /* Expand */
        int len = (int)(dt * 55 / (dur / 2));   /* lines: 0 → 55px  */
        int r   = (int)(dt *  8 / (dur / 2));   /* square: 0 → 8px  */

        tft_draw_line(cx - len, cy,       cx + len, cy,       COL_FLASH);
        tft_draw_line(cx,       cy - len, cx,       cy + len, COL_FLASH);
        /* 45° diagonals at half length */
        int d = len / 2;
        tft_draw_line(cx - d, cy - d, cx + d, cy + d, COL_FLASH);
        tft_draw_line(cx + d, cy - d, cx - d, cy + d, COL_FLASH);

        /* Centre filled square — ONE fill_rect instead of 289 draw_pixel */
        if (r > 0) {
            tft_fill_rect(cx - r, cy - r, r * 2, r * 2, COL_FLASH);
        }
    } else {
        /* Contract — lines stay at max, square shrinks */
        int64_t fade = dt - dur / 2;
        int r = (int)(8 - fade * 8 / (dur / 2));

        tft_draw_line(cx - 55, cy,      cx + 55, cy,      COL_FLASH);
        tft_draw_line(cx,      cy - 55, cx,      cy + 55, COL_FLASH);
        tft_draw_line(cx - 27, cy - 27, cx + 27, cy + 27, COL_FLASH);
        tft_draw_line(cx + 27, cy - 27, cx - 27, cy + 27, COL_FLASH);

        if (r > 0) {
            tft_fill_rect(cx - r, cy - r, r * 2, r * 2, COL_FLASH);
        }
    }
}

/* ─── phase 2: typewriter badge ──────────────────────────────────────────── *
 * "ESP32" at 1x scale, centred. One character revealed per time slice.
 * Unrevealed characters shown as dim PAL_OUTLINE ghosts so the full
 * word width is always visible — a classic terminal typewriter effect.
 * 1x scale keeps the badge clearly narrower than the 2x logo below it,
 * establishing visual hierarchy without overflowing the 160px width.
 */
static void draw_typewriter(int64_t t)
{
    if (t < T_TYPE_START) return;

    const char *text = "ESP32";
    int len = (int)strlen(text);
    int64_t dt  = t - T_TYPE_START;
    int64_t dur = T_TYPE_END - T_TYPE_START;

    int revealed = (dt >= dur) ? len : (int)(len * dt / dur) + 1;
    if (revealed > len) revealed = len;

    /* Centre at 1x: each char is 6px wide (5px glyph + 1px gap) */
    int total_w = len * 6 - 1;
    int cx = (TFT_WIDTH - total_w) / 2;

    for (int i = 0; i < len; i++) {
        uint16_t col = (i < revealed) ? COL_BADGE : PAL_OUTLINE;
        font_draw_char(cx, Y_BADGE, text[i], col);
        cx += 6;
    }
}

/* ─── phase 3: logo drop-in ─────────────────────────────────────────────── *
 * "GAME" (blue) and "CONSOLE" (gold) ease-out drop from above the screen.
 * Both start off the top edge and decelerate into their final Y positions.
 */
static void draw_logo_drop(int64_t t)
{
    if (t < T_DROP_START) return;

    int64_t dt  = t - T_DROP_START;
    int64_t dur = T_DROP_END - T_DROP_START;

    int x_game    = (TFT_WIDTH - splash_font_word_width("GAME"))    / 2;
    int x_console = (TFT_WIDTH - splash_font_word_width("CONSOLE")) / 2;

    int y_game    = ease_out(-20, Y_LOGO,  dt, dur);
    int y_console = ease_out(-20, Y_LOGO2, dt, dur);

    splash_font_draw_word(x_game,    y_game,    "GAME",    COL_LOGO_A, SPLASH_STYLE_OUTLINE);
    splash_font_draw_word(x_console, y_console, "CONSOLE", COL_LOGO_B, SPLASH_STYLE_OUTLINE);
}

/* ─── phase 4: scanline sweep ────────────────────────────────────────────── *
 * Bright horizontal bar sweeps top-to-bottom. Classic CRT "screen on"
 * visual cue. Five draw_line calls per frame — well within budget.
 */
static void draw_scanline(int64_t t)
{
    if (t < T_SCAN_START || t >= T_SCAN_END) return;

    int64_t dt  = t - T_SCAN_START;
    int64_t dur = T_SCAN_END - T_SCAN_START;

    int y = lerp(0, TFT_HEIGHT, dt, dur);

    uint16_t dim_col = PAL_OUTLINE;
    if (y > 1)            { tft_draw_line(0, y - 2, TFT_WIDTH - 1, y - 2, dim_col); }
    if (y > 0)            { tft_draw_line(0, y - 1, TFT_WIDTH - 1, y - 1, COL_SCANLINE); }
    tft_draw_line(0, y, TFT_WIDTH - 1, y, COL_FLASH);
    if (y < TFT_HEIGHT-1) { tft_draw_line(0, y + 1, TFT_WIDTH - 1, y + 1, COL_SCANLINE); }
    if (y < TFT_HEIGHT-2) { tft_draw_line(0, y + 2, TFT_WIDTH - 1, y + 2, dim_col); }
}

/* ─── phase 5: hold ─────────────────────────────────────────────────────── */
static void draw_hold(bool blink)
{
    /* Badge — "ESP32" at 1x, centred above logo */
    {
        const char *badge = "ESP32";
        int len = (int)strlen(badge);
        int total_w = len * 6 - 1;
        int bx = (TFT_WIDTH - total_w) / 2;
        int cx = bx;
        for (const char *p = badge; *p; p++) {
            font_draw_char(cx, Y_BADGE, *p, COL_BADGE);
            cx += 6;
        }
    }

    /* Main logo — OUTLINE weight for crispness */
    int x_game    = (TFT_WIDTH - splash_font_word_width("GAME"))    / 2;
    int x_console = (TFT_WIDTH - splash_font_word_width("CONSOLE")) / 2;
    splash_font_draw_word(x_game,    Y_LOGO,  "GAME",    COL_LOGO_A, SPLASH_STYLE_OUTLINE);
    splash_font_draw_word(x_console, Y_LOGO2, "CONSOLE", COL_LOGO_B, SPLASH_STYLE_OUTLINE);

    /* Separator */
    tft_draw_line(20, Y_SEP, TFT_WIDTH - 20, Y_SEP, PAL_OUTLINE);

    /* Blinking prompt */
    if (blink) {
        font_draw_str_centred(Y_HINT, "PRESS BTN TO START", COL_HINT, TFT_WIDTH);
    }
}

/* ─── public API ─────────────────────────────────────────────────────────── */

void splash_start(void)
{
    s_start_ms   = sp_now();
    s_blink      = true;
    s_blink_tick = s_start_ms;
    s_stars_init = false;
}

bool splash_tick(void)
{
    int64_t now = sp_now();
    int64_t t   = now - s_start_ms;

    if ((now - s_blink_tick) >= 450) {
        s_blink      = !s_blink;
        s_blink_tick = now;
    }

    if (t >= T_TIMEOUT) return false;

    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COL_BG);

    /* Phase 0: blank */
    if (t < T_FLASH_START) {
        tft_update();
        return true;
    }

    /* Stars — init once, draw every frame from phase 1 onward */
    if (!s_stars_init) {
        effects_stars_init(TFT_WIDTH, TFT_HEIGHT);
        s_stars_init = true;
    }
    effects_stars_draw();

    /* Phase 1: starburst */
    if (t < T_FLASH_END) {
        draw_flash(t);
        tft_update();
        return true;
    }

    /* Phase 2: typewriter badge */
    draw_typewriter(t);

    /* Phase 3: logo drop */
    if (t >= T_DROP_START) {
        draw_logo_drop(t);
    }

    /* Phase 4: scanline — redraw logo underneath so bar overlays it */
    if (t >= T_SCAN_START && t < T_SCAN_END) {
        int x_game    = (TFT_WIDTH - splash_font_word_width("GAME"))    / 2;
        int x_console = (TFT_WIDTH - splash_font_word_width("CONSOLE")) / 2;
        splash_font_draw_word(x_game,    Y_LOGO,  "GAME",    COL_LOGO_A, SPLASH_STYLE_OUTLINE);
        splash_font_draw_word(x_console, Y_LOGO2, "CONSOLE", COL_LOGO_B, SPLASH_STYLE_OUTLINE);
        draw_scanline(t);
    }

    /* Phase 5: hold */
    if (t >= T_HOLD_START) {
        draw_hold(s_blink);
    }

    tft_update();
    return true;
}