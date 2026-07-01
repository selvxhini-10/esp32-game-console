/*
 * Boot splash — animated title screen. 160x128 landscape color version.
 *
 * LAYOUT NOTE: this is a from-scratch landscape layout (TFT_WIDTH=160,
 * TFT_HEIGHT=128 after the orientation fix) — NOT the same numbers as
 * the earlier portrait (128x160) version. Y positions in particular
 * needed to shrink to fit the now-shorter 128px height.
 *
 * STYLE NOTE: each word now uses a distinct splash_font.h style
 * (BOLD / OUTLINE / GLOW) for visual variety during the animation,
 * dispatched through splash_font_draw_word(). See splash_font.h/.c
 * for why SPLASH_STYLE_SLANTED is its own explicit case rather than an
 * accidental fallback — that was the root cause of the "slanted, off
 * the viewport" bug from the previous version.
 *
 * CENTERING NOTE: X_ESP32/X_GAME/X_CONSOLE now use
 * splash_font_word_width(), which matches the EXACT per-character
 * advance (11px) the splash_font.c renderers use, instead of a
 * hand-rolled formula that had drifted out of sync with the renderer
 * and caused a few pixels of off-centre drift per word.
 *
 * Sequence (timing unchanged):
 *   0– 300ms  blank (display settle)
 *   300– 800ms "ESP32"   slides in from left,  BOLD style
 *   800–1300ms "GAME"    column-reveal left-to-right
 *  1300–1800ms "CONSOLE" slides in from right, OUTLINE style
 *  1800–2600ms full title (GLOW/OUTLINE/BOLD) + blinking "PRESS BTN"
 *  2600ms+    holds indefinitely until app_main breaks the loop on button press
 */

#include "splash.h"
#include "tft.h"
#include "font.h"
#include "splash_font.h"
#include "esp_timer.h"
#include <string.h>

/* ─── timing helper ──────────────────────────────────────────────────────── */

static int lerp(int a, int b, int64_t t, int64_t dur)
{
    if (t <= 0)   return a;
    if (t >= dur) return b;
    return (int)(a + (b - a) * t / dur);
}

/* ─── layout — landscape 160x128 ─────────────────────────────────────────── *
 *
 * Three 2x words + hint, fit inside 128px height:
 *   "ESP32"   y=10  (16px tall -> ends y=26)
 *   "GAME"    y=42  (16px tall -> ends y=58)
 *   "CONSOLE" y=74  (16px tall -> ends y=90)
 *   "PRESS BTN" hint y=108 (1x font, 7px tall -> ends y=115, inside 128px)
 *
 * X positions computed from splash_font_word_width() — the SAME advance
 * the renderers actually use — rather than a separately hand-calculated
 * formula that can silently drift out of sync (see splash_font.h's
 * root-cause comment for why that drift mattered).
 * ────────────────────────────────────────────────────────────────────────── */
#define Y_ESP32    10
#define Y_GAME     42
#define Y_CONSOLE  74
#define Y_HINT     108

#define COL_ESP32    TFT_CYAN
#define COL_GAME     TFT_YELLOW
#define COL_CONSOLE  TFT_GREEN
#define COL_HINT     TFT_WHITE
#define COL_BG       TFT_BLACK

/* ─── timing (ms) ─────────────────────────────────────────────────────────── */
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

void splash_start(void)
{
    s_start_ms   = sp_now();
    s_blink      = true;
    s_blink_tick = s_start_ms;
}

bool splash_tick(void)
{
    int64_t now = sp_now();
    int64_t t   = now - s_start_ms;

    if ((now - s_blink_tick) >= 400) {
        s_blink      = !s_blink;
        s_blink_tick = now;
    }

    if (t >= T_HOLD_END + 10000) return false;   /* 10s safety timeout */

    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COL_BG);

    if (t < T_BLANK_END) {
        tft_update();
        return true;
    }

    /* X positions computed fresh each call from the live word-width
     * helper — cheap (just a strlen + multiply), and guarantees these
     * never drift out of sync with whatever the renderers actually draw. */
    int x_esp32   = (TFT_WIDTH - splash_font_word_width("ESP32"))   / 2;
    int x_game    = (TFT_WIDTH - splash_font_word_width("GAME"))    / 2;
    int x_console = (TFT_WIDTH - splash_font_word_width("CONSOLE")) / 2;

    /* ── ESP32: slide in from left, BOLD style ── */
    if (t >= T_ESP32_START) {
        int x = lerp(-splash_font_word_width("ESP32"), x_esp32,
                     t - T_ESP32_START, T_ESP32_END - T_ESP32_START);
        splash_font_draw_word(x, Y_ESP32, "ESP32", COL_ESP32, SPLASH_STYLE_BOLD);
    }

    /* ── GAME: left-to-right column reveal ── */
    if (t >= T_GAME_START) {
        int64_t dt  = t - T_GAME_START;
        int64_t dur = T_GAME_END - T_GAME_START;
        int reveal_cols = (dt >= dur) ? 4 : (int)(4 * dt / dur) + 1;
        if (reveal_cols > 4) reveal_cols = 4;
        splash_font_draw_2x_column_reveal(x_game, Y_GAME, "GAME", COL_GAME, reveal_cols);
    }

    /* ── CONSOLE: slide in from right, OUTLINE style ── */
    if (t >= T_CON_START) {
        int x = lerp(TFT_WIDTH, x_console,
                     t - T_CON_START, T_CON_END - T_CON_START);
        splash_font_draw_word(x, Y_CONSOLE, "CONSOLE", COL_CONSOLE, SPLASH_STYLE_OUTLINE);
    }

    /* ── Final hold: full stylized state + blinking hint ── */
    if (t >= T_CON_END) {
        splash_font_draw_word(x_esp32,   Y_ESP32,   "ESP32",   COL_ESP32,   SPLASH_STYLE_GLOW);
        splash_font_draw_word(x_game,    Y_GAME,    "GAME",    COL_GAME,    SPLASH_STYLE_OUTLINE);
        splash_font_draw_word(x_console, Y_CONSOLE, "CONSOLE", COL_CONSOLE, SPLASH_STYLE_BOLD);

        if (s_blink) {
            font_draw_str_centred(Y_HINT, "PRESS BTN TO START", COL_HINT, TFT_WIDTH);
        }
    }

    tft_update();
    return true;
}