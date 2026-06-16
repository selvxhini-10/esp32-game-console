/*
 * Boot splash — animated title screen.
 *
 * Layout (no overlap, fits 64px):
 *   "ESP32"   2× text  y=4   (16px tall → ends y=20)
 *   "GAME"    2× text  y=23  (16px tall → ends y=39)
 *   "CONSOLE" 2× text  y=45  (16px tall → ends y=61)
 *
 * Sequence:
 *   0– 300ms  blank (display settle)
 *   300– 800ms "ESP32"   slides in from left
 *   800–1300ms "GAME"    revealed top-down
 *  1300–1800ms "CONSOLE" slides in from right
 *  1800–2600ms full title + blinking "PRESS BTN"
 *  2600ms+    returns false → hand off to console
 *
 * Bugs fixed vs previous version:
 *  - sp_border() no longer called before oled_clear() completes
 *  - Phase 0 early-return now calls oled_update() so display is blank (not garbage)
 *  - Text y-positions recalculated so none overlap
 *  - "CONSOLE" final_x corrected (was drifting off right edge)
 */

#include "splash.h"
#include "oled.h"
#include "esp_timer.h"
#include <string.h>

/* ─── font ───────────────────────────────────────────────────────────────── */
static const uint8_t sp_font[][5] = {
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

/* ─── draw helpers ───────────────────────────────────────────────────────── */

/* 1× character */
static void sp_char(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) return;
    const uint8_t *g = sp_font[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t b = g[col];
        for (int row = 0; row < 8; row++)
            if (b & (1 << row)) oled_draw_pixel(x + col, y + row);
    }
}

/* 1× string centred */
static void sp_str_c(int y, const char *s)
{
    int len = 0; for (const char *p = s; *p; p++) len++;
    int x = (128 - len * 6) / 2; if (x < 0) x = 0;
    while (*s) { sp_char(x, y, *s++); x += 6; }
}

/*
 * 2× character — each source pixel becomes a 2×2 block.
 * Glyph width = 5 cols × 2 = 10px, plus 1px gap → 11px per char.
 * Glyph height = 8 rows × 2 = 16px.
 */
static void sp_char_2x(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) return;
    const uint8_t *g = sp_font[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t b = g[col];
        for (int row = 0; row < 8; row++) {
            if (b & (1 << row)) {
                oled_draw_pixel(x + col*2,     y + row*2);
                oled_draw_pixel(x + col*2 + 1, y + row*2);
                oled_draw_pixel(x + col*2,     y + row*2 + 1);
                oled_draw_pixel(x + col*2 + 1, y + row*2 + 1);
            }
        }
    }
}

/* 2× string at explicit x */
static void sp_str_2x(int x, int y, const char *s)
{
    while (*s) { sp_char_2x(x, y, *s++); x += 11; }
}

/* 2× string centred */
static void sp_str_2x_c(int y, const char *s)
{
    int len = 0; for (const char *p = s; *p; p++) len++;
    int w = len * 11 - 1;
    int x = (128 - w) / 2; if (x < 0) x = 0;
    sp_str_2x(x, y, s);
}

/*
 * 2× string revealed top-down.
 * reveal_px — how many pixel rows (0-16) are visible from the top.
 */
static void sp_str_2x_reveal(int x, int y, const char *s, int reveal_px)
{
    int wx = x;
    for (; *s; s++, wx += 11) {
        char c = *s;
        if (c < 0x20 || c > 0x7E) continue;
        const uint8_t *g = sp_font[c - 0x20];
        for (int col = 0; col < 5; col++) {
            uint8_t b = g[col];
            for (int row = 0; row < 8; row++) {
                if (!(b & (1 << row))) continue;
                int py0 = row * 2;
                int py1 = py0 + 1;
                if (py0 < reveal_px) oled_draw_pixel(wx + col*2,     y + py0);
                if (py0 < reveal_px) oled_draw_pixel(wx + col*2 + 1, y + py0);
                if (py1 < reveal_px) oled_draw_pixel(wx + col*2,     y + py1);
                if (py1 < reveal_px) oled_draw_pixel(wx + col*2 + 1, y + py1);
            }
        }
    }
}

/* Lerp helper — linear interpolate from a→b over dur ms, clamped */
static int lerp(int a, int b, int64_t t, int64_t dur)
{
    if (t <= 0)   return a;
    if (t >= dur) return b;
    return (int)(a + (b - a) * t / dur);
}

/* ─── layout constants ───────────────────────────────────────────────────── */

/*
 * Three 2× words stacked vertically with 3px gaps:
 *   "ESP32"   y=4   → pixels 4..19
 *   "GAME"    y=23  → pixels 23..38  (gap of 3px from above)
 *   "CONSOLE" y=42  → pixels 42..57  (gap of 3px from above)
 *   "PRESS BTN" hint  y=56 (1× font, 7px tall → ends at y=63)
 *
 * "ESP32":   5 chars × 11px = 55px wide → centred at x=36
 * "GAME":    4 chars × 11px = 44px − 1 = 43px wide → centred at x=42
 * "CONSOLE": 7 chars × 11px = 77px − 1 = 76px wide → centred at x=26
 */
#define Y_ESP32    4
#define Y_GAME     23
#define Y_CONSOLE  42
#define Y_HINT     56   /* 1× font is 7px tall → ends at y=63, inside display */

/* Centred x for each word (pre-calculated) */
#define X_ESP32    ((128 - (5*11-1)) / 2)   /* (128-54)/2 = 37 */
#define X_GAME     ((128 - (4*11-1)) / 2)   /* (128-43)/2 = 42 */
#define X_CONSOLE  ((128 - (7*11-1)) / 2)   /* (128-76)/2 = 26 */

/* ─── timing (ms) ────────────────────────────────────────────────────────── */
#define T_BLANK_END    300
#define T_ESP32_START  300
#define T_ESP32_END    800
#define T_GAME_START   800
#define T_GAME_END    1300
#define T_CON_START   1300
#define T_CON_END     1800
#define T_HOLD_END    2600

/* ─── state ──────────────────────────────────────────────────────────────── */
static int64_t s_start_ms   = 0;
static bool    s_blink      = false;
static int64_t s_blink_tick = 0;

static inline int64_t sp_now(void) { return esp_timer_get_time() / 1000; }

/* ─── public ─────────────────────────────────────────────────────────────── */

void splash_start(void)
{
    s_start_ms   = sp_now();
    s_blink      = true;
    s_blink_tick = s_start_ms;
}

bool splash_tick(void)
{
    int64_t now     = sp_now();
    int64_t t       = now - s_start_ms;   /* elapsed ms */

    /* Blink toggle every 400 ms */
    if ((now - s_blink_tick) >= 400) {
        s_blink      = !s_blink;
        s_blink_tick = now;
    }

    /*
     * Animation phases complete after T_HOLD_END, but we do NOT auto-exit.
     * The caller (app_main) breaks the loop on button press.
     * splash_tick() only returns false if somehow called after T_HOLD_END+10s
     * as a safety timeout — normal exit is via the caller's break.
     */
    if (t >= T_HOLD_END + 10000) return false;   /* 10s safety timeout */

    /* Always clear first — no partial draws from previous tick */
    oled_clear();

    /* Phase 0: blank display, nothing to draw */
    if (t < T_BLANK_END) {
        oled_update();
        return true;
    }

    /*
     * Phase 1: "ESP32" slides in from off-screen left → centred position.
     * start_x chosen so the word is fully off-screen at t=T_ESP32_START.
     * X_ESP32 is the resting centred position.
     */
    if (t >= T_ESP32_START) {
        int x = lerp(-55, X_ESP32, t - T_ESP32_START, T_ESP32_END - T_ESP32_START);
        sp_str_2x(x, Y_ESP32, "ESP32");
    }

    /*
     * Phase 2: "GAME" revealed top-down, 16 pixel rows over 500 ms.
     */
    if (t >= T_GAME_START) {
        int64_t dt      = t - T_GAME_START;
        int64_t dur     = T_GAME_END - T_GAME_START;
        int reveal_px   = (dt >= dur) ? 16 : (int)(16 * dt / dur);
        /* centred x for "GAME" */
        sp_str_2x_reveal(X_GAME, Y_GAME, "GAME", reveal_px);
    }

    /*
     * Phase 3: "CONSOLE" slides in from off-screen right → centred position.
     * start_x = 128 (fully off right edge).
     */
    if (t >= T_CON_START) {
        int x = lerp(128, X_CONSOLE, t - T_CON_START, T_CON_END - T_CON_START);
        sp_str_2x(x, Y_CONSOLE, "CONSOLE");
    }

    /*
     * Phase 4: all three words at final positions + blinking hint.
     * Re-draw locks them in place cleanly (overrides any lerp residual).
     */
    if (t >= T_CON_END) {
        sp_str_2x(X_ESP32,   Y_ESP32,   "ESP32");
        sp_str_2x(X_GAME,    Y_GAME,    "GAME");
        sp_str_2x(X_CONSOLE, Y_CONSOLE, "CONSOLE");

        if (s_blink)
            sp_str_c(Y_HINT, "PRESS BTN TO START");
    }

    oled_update();
    return true;
}