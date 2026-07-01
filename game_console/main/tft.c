/*
 * TFT — ST7735S 128x160 SPI color display driver implementation.
 *
 * ── INIT SEQUENCE NOTES ──────────────────────────────────────────────────
 * The ST7735S init sequence below is the standard sequence used by most
 * ST7735 driver implementations (Adafruit's included). It configures:
 *   - Software reset + sleep out
 *   - Frame rate control
 *   - Power control sequence (the three PWCTR commands)
 *   - VCOM voltage
 *   - Memory access control (sets pixel read/write direction + RGB order)
 *   - Color mode: 16-bit RGB565 (COLMOD = 0x05)
 *   - Display inversion OFF, normal mode, display ON
 *
 * If your specific ST7735S module shows mirrored or color-swapped output,
 * the two values to adjust are MADCTL (orientation/RGB-BGR order) and
 * the gamma curve commands — both flagged below.
 *
 * ── FRAMEBUFFER STRATEGY ─────────────────────────────────────────────────
 * Unlike the 1KB OLED framebuffer, this is 128*160*2 = 40,960 bytes. That's
 * comfortably within a plain ESP32's 520KB SRAM, but it's too large to put
 * on a task's stack — it's declared as a static array.
 *
 * ── DIRTY RECTANGLE OPTIMISATION ─────────────────────────────────────────
 * Sending the full 40KB framebuffer over SPI every frame, even at 26MHz
 * (ST7735's typical max), takes meaningful time:
 *     40960 bytes * 8 bits / 26,000,000 Hz ≈ 12.6ms per full update
 * That's a noticeable chunk of a 16-33ms game frame budget. Games rarely
 * change the WHOLE screen every frame (e.g. Snake only changes a few
 * cells), so tft_update() tracks a dirty bounding box — the smallest
 * rectangle containing every pixel changed since the last update — and
 * only transfers that region. Worst case (every pixel changed) costs the
 * same ~12.6ms as before; typical case is dramatically cheaper.
 */

#include "tft.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "tft";

/* ── pin configuration — adjust to match your physical wiring ───────────── */
#define PIN_MOSI   23
#define PIN_CLK    18
#define PIN_CS      5
#define PIN_DC      2
#define PIN_RST     4

/*
 * PIN_BL — backlight control pin.
 *
 * Most ST7735S breakout modules expose a separate BLK/BL/LED pin
 * alongside the 5 SPI/control lines above. Unlike SH1106/SSD1306 OLEDs
 * (which generate their own light per-pixel and have no backlight),
 * a TFT panel is backlit by a physical LED that must be powered
 * independently of the SPI data — sending perfectly correct pixel data
 * over SPI does nothing visible if the backlight LED never turns on.
 *
 * If your specific module ties BLK directly to 3V3 with no separate pin
 * exposed, you can ignore this entirely (backlight is always on as soon
 * as the board is powered) — but if your module has 8 pins (the common
 * case), this pin MUST be driven HIGH in software, or the screen will
 * appear completely dark even though the SPI commands are working.
 */
#define PIN_BL      15   /* change to whichever free GPIO you wired BLK to */

static spi_device_handle_t tft_spi;

/* ── framebuffer: RGB565, one uint16_t per pixel ─────────────────────────── */
static uint16_t framebuffer[TFT_WIDTH * TFT_HEIGHT];

/* ── dirty rectangle tracking ─────────────────────────────────────────────── */
static int s_dirty_x0, s_dirty_y0, s_dirty_x1, s_dirty_y1;
static bool s_dirty = false;

static inline void mark_dirty(int x, int y)
{
    if (!s_dirty) {
        s_dirty_x0 = s_dirty_x1 = x;
        s_dirty_y0 = s_dirty_y1 = y;
        s_dirty = true;
        return;
    }
    if (x < s_dirty_x0) s_dirty_x0 = x;
    if (x > s_dirty_x1) s_dirty_x1 = x;
    if (y < s_dirty_y0) s_dirty_y0 = y;
    if (y > s_dirty_y1) s_dirty_y1 = y;
}

/* ── low level SPI helpers ───────────────────────────────────────────────── */

static void tft_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_transmit(tft_spi, &t);
}

static void tft_data(const uint8_t *data, int len)
{
    gpio_set_level(PIN_DC, 1);
    spi_transaction_t t = { .length = (size_t)len * 8, .tx_buffer = data };
    spi_device_transmit(tft_spi, &t);
}

static void tft_data8(uint8_t d)
{
    tft_data(&d, 1);
}

/*
 * Set the addressing window for the next data write (standard ST7735 pattern).
 *
 * ── CORRECTION — a previous version of this function manually swapped
 * x<->y before sending to CASET/RASET, based on a flawed assumption that
 * MADCTL's MV bit doesn't affect how the controller interprets those two
 * commands. That assumption was WRONG: MV's actual job is to tell the
 * controller "relabel CASET/RASET so they match the caller's rotated
 * logical axes" — the controller does this relabeling internally. Manually
 * swapping x/y in software on top of that double-rotates the addressing,
 * which is likely WORSE than the original problem, not a fix for it.
 *
 * This version sends LOGICAL x0/y0/x1/y1 directly to CASET/RASET, exactly
 * as the controller expects once MV is set correctly — no manual swap.
 *
 * If content still appears torn, shifted, or wraps onto the wrong rows
 * after this fix, the most likely remaining cause is one of:
 *   (a) MADCTL value (0x36) is the wrong rotation for this specific
 *       module/PCB — try 0x60, 0xA0, or 0x68 instead of 0xE0 (see the
 *       bit-meaning table in tft_init() above this function)
 *   (b) COL_OFFSET/ROW_OFFSET below need different values, or need to
 *       or need to be 0 — these are a per-module manufacturing quirk,
 *       not a universal constant, and have to be found by testing on
 *       the actual hardware rather than guessed in advance
 */
static void tft_set_window(int x0, int y0, int x1, int y1)
{
    /*
     * Per-module column/row offset — many ST7735S boards have a small
     * GRAM offset between the controller's internal addressing and the
     * visible glass. Set both to 0 first and test; only add an offset
     * back if you see a thin colored strip along one edge or clipped
     * content on the opposite edge.
     */
    #define COL_OFFSET  0
    #define ROW_OFFSET  0

    uint8_t buf[4];

    tft_cmd(0x2A); /* CASET — column address set, logical X range */
    buf[0] = 0x00; buf[1] = (uint8_t)(x0 + COL_OFFSET);
    buf[2] = 0x00; buf[3] = (uint8_t)(x1 + COL_OFFSET);
    tft_data(buf, 4);

    tft_cmd(0x2B); /* RASET — row address set, logical Y range */
    buf[0] = 0x00; buf[1] = (uint8_t)(y0 + ROW_OFFSET);
    buf[2] = 0x00; buf[3] = (uint8_t)(y1 + ROW_OFFSET);
    tft_data(buf, 4);

    tft_cmd(0x2C); /* RAMWR — memory write */
}

/* ── init sequence ────────────────────────────────────────────────────────── */

void tft_init(void)
{
    gpio_set_direction(PIN_DC,  GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);

    /*
     * Backlight ON.
     *
     * This must happen before (or at worst, around the same time as) the
     * SPI init sequence below — the backlight LED is just an LED, it has
     * no dependency on the display controller being initialised. Turning
     * it on early also means you'll see a brief flash of the OLD frame
     * memory contents (random colors / garbage) right after power-on,
     * which is normal and confirms the panel + backlight are physically
     * working — that goes away once tft_clear() + tft_update() run later
     * in this function.
     */
    gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BL, 1);

    spi_bus_config_t buscfg = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2,  /* full frame in one go */
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 26 * 1000 * 1000,  /* 26MHz — ST7735S typical max */
        .mode           = 0,
        .spics_io_num   = PIN_CS,
        .queue_size     = 1,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &tft_spi);

    /* Hardware reset */
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    tft_cmd(0x01); /* SWRESET — software reset */
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_cmd(0x11); /* SLPOUT — sleep out */
    vTaskDelay(pdMS_TO_TICKS(255));

    /* Frame rate control — normal mode */
    tft_cmd(0xB1); tft_data8(0x01); tft_data8(0x2C); tft_data8(0x2D);
    tft_cmd(0xB2); tft_data8(0x01); tft_data8(0x2C); tft_data8(0x2D);
    tft_cmd(0xB3); tft_data8(0x01); tft_data8(0x2C); tft_data8(0x2D);
    tft_data8(0x01); tft_data8(0x2C); tft_data8(0x2D);

    tft_cmd(0xB4); tft_data8(0x07); /* display inversion control */

    /* Power control */
    tft_cmd(0xC0); tft_data8(0xA2); tft_data8(0x02); tft_data8(0x84);
    tft_cmd(0xC1); tft_data8(0xC5);
    tft_cmd(0xC2); tft_data8(0x0A); tft_data8(0x00);
    tft_cmd(0xC3); tft_data8(0x8A); tft_data8(0x2A);
    tft_cmd(0xC4); tft_data8(0x8A); tft_data8(0xEE);

    tft_cmd(0xC5); tft_data8(0x0E); /* VCOM control */

    /*
     * MADCTL — Memory Access Control. This byte controls rotation and
     * RGB/BGR color order.
     *
     * Bit layout: MY MX MV ML RGB MH - -
     *   MY (0x80) = row address order (flip vertically)
     *   MX (0x40) = column address order (flip horizontally)
     *   MV (0x20) = row/column exchange (the actual 90° rotation bit)
     *   RGB (0x08) = 0 for RGB order, 1 for BGR order
     *
     * Changed default from 0xE0 to 0x68 (MX | MV | RGB-bit) — the
     * previous working PORTRAIT config was 0xC0 (MY|MX, RGB order off,
     * i.e. BGR panel). 0x68 is the landscape equivalent for a BGR panel:
     * MV rotates 90°, MX+RGB-bit together select the corner/color-order
     * combination most ST7735S breakouts need for "pins left, label
     * right" with correct (non-swapped) red/blue.
     *
     * IF ORIENTATION IS STILL WRONG, call tft_test_pattern() (declared
     * in tft.h) right after tft_init() in app_main, BEFORE anything else
     * draws to the screen. It paints each corner a distinct, named
     * color and prints to serial which corner should show which color —
     * compare that against what you actually see and pick the matching
     * MADCTL value from the candidates below without any more guessing:
     *
     *   0x00 = no rotation,        RGB        | 0x08 = no rotation,        BGR
     *   0x60 = landscape (MX|MV),  RGB        | 0x68 = landscape (MX|MV),  BGR  <- current default
     *   0xA0 = landscape (MY|MV),  RGB        | 0xA8 = landscape (MY|MV),  BGR
     *   0xC0 = portrait  (MY|MX),  RGB        | 0xC8 = portrait  (MY|MX),  BGR  <- old working portrait value
     */
    tft_cmd(0x36); tft_data8(0x68);

    /*
     * COLMOD — color mode. 0x05 = 16-bit/pixel (RGB565), which is what
     * this entire driver assumes throughout. Do not change this value
     * without also rewriting the framebuffer format.
     */
    tft_cmd(0x3A); tft_data8(0x05);

    tft_cmd(0x29); /* DISPON — display on */
    vTaskDelay(pdMS_TO_TICKS(100));

    tft_clear(TFT_BLACK);
    tft_update();

    ESP_LOGI(TAG, "ST7735S initialised: %dx%d RGB565", TFT_WIDTH, TFT_HEIGHT);
}

/* ── drawing primitives ───────────────────────────────────────────────────── */

void tft_clear(uint16_t color)
{
    for (int i = 0; i < TFT_WIDTH * TFT_HEIGHT; i++) {
        framebuffer[i] = color;
    }
    /* Clearing touches the whole screen — mark the entire buffer dirty */
    s_dirty_x0 = 0; s_dirty_y0 = 0;
    s_dirty_x1 = TFT_WIDTH - 1; s_dirty_y1 = TFT_HEIGHT - 1;
    s_dirty = true;
}

/*
 * tft_test_pattern() — diagnostic helper for finding the correct MADCTL
 * value empirically, instead of guessing blindly.
 *
 * Call this ONCE, right after tft_init(), before anything else draws —
 * for example, temporarily add `tft_test_pattern();` as the very first
 * line inside app_main() after tft_init() returns, reflash, and look at
 * the physical screen.
 *
 * It paints each of the four corners and the centre a distinct color and
 * logs what SHOULD be where. Compare the log against the physical
 * display:
 *   - If colors are in the WRONG corners (e.g. red appears top-right
 *     instead of top-left), the MV/MX/MY bits in MADCTL (0x36) need a
 *     different combination — see the candidate list in tft_init().
 *   - If colors are in the RIGHT corners but RED and BLUE look swapped
 *     (a red square looks blue and vice versa), toggle the RGB bit
 *     (add or remove 0x08 from the current MADCTL value).
 *   - If everything matches the log exactly, orientation is correct and
 *     any remaining visual issue is unrelated to MADCTL.
 */
void tft_test_pattern(void)
{
    int w = 20, h = 20;
    tft_clear(TFT_BLACK);

    tft_fill_rect(0, 0, w, h, TFT_RED);                                  /* top-left */
    tft_fill_rect(TFT_WIDTH - w, 0, w, h, TFT_GREEN);                    /* top-right */
    tft_fill_rect(0, TFT_HEIGHT - h, w, h, TFT_BLUE);                    /* bottom-left */
    tft_fill_rect(TFT_WIDTH - w, TFT_HEIGHT - h, w, h, TFT_YELLOW);      /* bottom-right */
    tft_fill_rect((TFT_WIDTH - w) / 2, (TFT_HEIGHT - h) / 2, w, h, TFT_WHITE); /* centre */

    tft_update();

    ESP_LOGI(TAG, "TEST PATTERN — expected layout (logical %dx%d frame):",
             TFT_WIDTH, TFT_HEIGHT);
    ESP_LOGI(TAG, "  RED    = top-left");
    ESP_LOGI(TAG, "  GREEN  = top-right");
    ESP_LOGI(TAG, "  BLUE   = bottom-left");
    ESP_LOGI(TAG, "  YELLOW = bottom-right");
    ESP_LOGI(TAG, "  WHITE  = centre");
    ESP_LOGI(TAG, "Compare against the physical screen, then pick the");
    ESP_LOGI(TAG, "matching MADCTL value from the candidates listed above");
    ESP_LOGI(TAG, "the tft_cmd(0x36) line in tft_init().");
}

void tft_draw_pixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= TFT_WIDTH || y < 0 || y >= TFT_HEIGHT) return;
    framebuffer[y * TFT_WIDTH + x] = color;
    mark_dirty(x, y);
}

void tft_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int i = 0; i < w; i++) {
        tft_draw_pixel(x + i, y,         color);
        tft_draw_pixel(x + i, y + h - 1, color);
    }
    for (int i = 0; i < h; i++) {
        tft_draw_pixel(x,         y + i, color);
        tft_draw_pixel(x + w - 1, y + i, color);
    }
}

void tft_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    /* Clip to screen bounds once, then write directly into the
     * framebuffer without per-pixel bounds checks — this is the hot
     * path for backgrounds and large UI panels, worth the extra code
     * to keep it fast. */
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > TFT_WIDTH)  ? TFT_WIDTH  : x + w;
    int y1 = (y + h > TFT_HEIGHT) ? TFT_HEIGHT : y + h;
    if (x0 >= x1 || y0 >= y1) return;

    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = &framebuffer[yy * TFT_WIDTH + x0];
        for (int xx = x0; xx < x1; xx++) {
            *row++ = color;
        }
    }
    mark_dirty(x0, y0);
    mark_dirty(x1 - 1, y1 - 1);
}

void tft_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    /* Standard Bresenham integer line algorithm */
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x1 >= x0) ? 1 : -1;
    int sy = (y1 >= y0) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        tft_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void tft_draw_circle(int cx, int cy, int r, uint16_t color)
{
    /* Midpoint circle algorithm — draws the 8-way symmetric outline */
    int x = r, y = 0;
    int err = 0;

    while (x >= y) {
        tft_draw_pixel(cx + x, cy + y, color);
        tft_draw_pixel(cx + y, cy + x, color);
        tft_draw_pixel(cx - y, cy + x, color);
        tft_draw_pixel(cx - x, cy + y, color);
        tft_draw_pixel(cx - x, cy - y, color);
        tft_draw_pixel(cx - y, cy - x, color);
        tft_draw_pixel(cx + y, cy - x, color);
        tft_draw_pixel(cx + x, cy - y, color);

        y++;
        err += 1 + 2*y;
        if (2*(err - x) + 1 > 0) {
            x--;
            err += 1 - 2*x;
        }
    }
}

void tft_fill_circle(int cx, int cy, int r, uint16_t color)
{
    /* Simple scanline fill — fine at the small radii used in these games */
    for (int y = -r; y <= r; y++) {
        int span = (int)(r * r - y * y);
        if (span < 0) continue;
        int half = (int)(sqrt((double)span));
        for (int x = -half; x <= half; x++) {
            tft_draw_pixel(cx + x, cy + y, color);
        }
    }
}

/* ── frame transfer ───────────────────────────────────────────────────────── */

void tft_update(void)
{
    if (!s_dirty) return;   /* nothing changed since last update — skip SPI entirely */

    int x0 = s_dirty_x0, y0 = s_dirty_y0;
    int x1 = s_dirty_x1, y1 = s_dirty_y1;

    tft_set_window(x0, y0, x1, y1);

    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;

    /*
     * Send row by row within the dirty rectangle. Sending non-contiguous
     * framebuffer rows means we can't do a single giant SPI transaction
     * for partial updates (the dirty rect's rows aren't back-to-back in
     * memory unless x0==0 and x1==TFT_WIDTH-1) — so each row becomes one
     * SPI transaction. This is still far cheaper than a full-frame send
     * for small dirty regions, which is the common case during gameplay.
     */
    gpio_set_level(PIN_DC, 1);
    for (int row = y0; row <= y1; row++) {
        spi_transaction_t t = {
            .length    = (size_t)w * 2 * 8,   /* w pixels * 2 bytes/pixel * 8 bits */
            .tx_buffer = &framebuffer[row * TFT_WIDTH + x0],
        };
        spi_device_transmit(tft_spi, &t);
    }

    (void)h;
    s_dirty = false;
}