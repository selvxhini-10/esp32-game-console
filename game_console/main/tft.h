#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TFT — ST7735S 128x160 SPI color display driver.
 *
 * Replaces oled.h/oled.c entirely. Same SPI wiring pattern as the old
 * SH1106/SSD1306 OLED (MOSI, CLK, CS, DC, RST) — physically just swap
 * the display module, the pins can stay the same if convenient.
 *
 * KEY DIFFERENCES FROM THE OLD MONOCHROME OLED DRIVER:
 *
 *   1. Resolution: 128x160 instead of 128x64 — 2.5x more pixels, and
 *      critically, 96 more vertical pixels (60 -> 160) which is the
 *      single biggest layout win for every game.
 *
 *   2. Color: RGB565 (16-bit color, 5 bits red / 6 bits green / 5 bits
 *      blue) instead of 1-bit monochrome. Every draw call now takes a
 *      color parameter — there is no more implicit "on" pixel.
 *
 *   3. Framebuffer size: 128*160*2 bytes = 40,960 bytes (40KB) instead
 *      of 1024 bytes. This is large enough that it should NOT live on
 *      the stack — it's a static/heap buffer. On a plain ESP32 (520KB
 *      SRAM) this is fine. On ESP32-WROVER it could optionally be
 *      placed in PSRAM if SRAM pressure ever becomes an issue (see
 *      tft_init() comments).
 *
 *   4. SPI transfer cost: at the same 8-10MHz SPI clock used before,
 *      a full-frame update is ~40x larger than before. This material
 *      -ly changes the "always send the whole frame" assumption from
 *      the OLED driver — see tft_update() comments for the dirty-
 *      rectangle optimisation this driver uses instead.
 */

/*
 * Landscape orientation: pins (1-8 header) on the LEFT, the panel's
 * printed "1.8 TFT128" label on the RIGHT. This is a 90° rotation from
 * the panel's native portrait orientation, so width/height swap from
 * the controller's native 128x160 to a logical 160x128 — every draw
 * call in this driver and every game already treats TFT_WIDTH/HEIGHT
 * as "however the screen is held", so no game code needs to change.
 */
#define TFT_WIDTH   160
#define TFT_HEIGHT  128

/* ── RGB565 color helpers ──────────────────────────────────────────────── */

/* Build an RGB565 value from 8-bit per-channel inputs (0-255 each) */
#define TFT_RGB(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

/* Common colors, pre-built for convenience */
#define TFT_BLACK    0x0000
#define TFT_WHITE    0xFFFF
#define TFT_RED      0xF800
#define TFT_GREEN    0x07E0
#define TFT_BLUE     0x001F
#define TFT_YELLOW   0xFFE0
#define TFT_CYAN     0x07FF
#define TFT_MAGENTA  0xF81F
#define TFT_ORANGE   0xFD20
#define TFT_GRAY     0x8410
#define TFT_DARKGRAY 0x4208
#define TFT_DARKGREEN 0x03E0
#define TFT_BROWN    0x9A60
#define TFT_PINK     0xFC18
#define TFT_PURPLE   0x8010
#define TFT_NAVY     0x000F
#define TFT_SKYBLUE  0x867D
#define TFT_GOLD     0xFEA0

/* ── public API ───────────────────────────────────────────────────────── */

void tft_init(void);

/* Clear the entire framebuffer to one color (replaces oled_clear) */
void tft_clear(uint16_t color);

/*
 * Diagnostic helper — paints a known color pattern to each corner so you
 * can empirically determine the correct MADCTL orientation/color-order
 * value for your specific module, instead of guessing. See the detailed
 * usage comment above this function's definition in tft.c. Call once,
 * right after tft_init(), temporarily, while tuning orientation.
 */
void tft_test_pattern(void);

/* Push the framebuffer (or just the dirty region) to the display over SPI */
void tft_update(void);

void tft_draw_pixel(int x, int y, uint16_t color);

void tft_draw_rect(int x, int y, int w, int h, uint16_t color);
void tft_fill_rect(int x, int y, int w, int h, uint16_t color);

/* Bresenham line — useful for diagonal effects color displays make worthwhile */
void tft_draw_line(int x0, int y0, int x1, int y1, uint16_t color);

void tft_draw_circle(int cx, int cy, int r, uint16_t color);
void tft_fill_circle(int cx, int cy, int r, uint16_t color);

#ifdef __cplusplus
}
#endif