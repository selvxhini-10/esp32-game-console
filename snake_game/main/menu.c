#include "menu.h"
#include "oled.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

/* =========================
   MINIMAL 5x7 FONT
   Only printable ASCII 0x20-0x7E stored as 5 columns of 8 bits.
   Each byte is one column, LSB = top pixel.
   ========================= */

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' 0x20 */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* '\'' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
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
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' */
    {0x3C,0x40,0x40,0x40,0x7C}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* '}' */
    {0x10,0x08,0x08,0x10,0x08}, /* '~' */
};

/* =========================
   INTERNAL STATE
   ========================= */

static MenuState  s_state       = MENU_STATE_MAIN;
static MainMenuItem s_selected  = MENU_ITEM_START;
static uint32_t   s_highscore   = 0;
static uint32_t   s_last_score  = 0;
static int        s_countdown   = 3;      /* 3-2-1 */
static uint32_t   s_countdown_tick = 0;   /* ms timestamp */
static bool       s_blink       = false;  /* cursor blink toggle */
static uint32_t   s_blink_tick  = 0;

/* debounce helpers */
static int  s_last_dy  = 0;
static bool s_last_btn = false;

/* =========================
   COLLISION HELPERS
   ========================= */

bool rect_in_bounds(const Rect *r)
{
    return (r->x >= 0 &&
            r->y >= 0 &&
            r->x + r->w <= OLED_WIDTH  &&
            r->y + r->h <= OLED_HEIGHT);
}

void rect_clamp_to_bounds(Rect *r)
{
    if (r->x < 0) r->x = 0;
    if (r->y < 0) r->y = 0;
    if (r->x + r->w > OLED_WIDTH)  r->x = OLED_WIDTH  - r->w;
    if (r->y + r->h > OLED_HEIGHT) r->y = OLED_HEIGHT - r->h;
}

bool rect_intersects(const Rect *a, const Rect *b)
{
    return !(a->x + a->w <= b->x ||
             b->x + b->w <= a->x ||
             a->y + a->h <= b->y ||
             b->y + b->h <= a->y);
}

/* =========================
   FONT RENDERING
   ========================= */

/* Draw one character; returns pixel width consumed (char width + 1 gap) */
static int draw_char(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';

    const uint8_t *glyph = font5x7[c - 0x20];

    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 8; row++) {
            if (bits & (1 << row)) {
                oled_draw_pixel(x + col, y + row);
            }
        }
    }
    return 6; /* 5px glyph + 1px kerning gap */
}

/* Draw a null-terminated string; returns total width in pixels */
static int draw_string(int x, int y, const char *str)
{
    int cx = x;
    while (*str) {
        cx += draw_char(cx, y, *str++);
    }
    return cx - x;
}

/* Horizontally-centred string */
static void draw_string_centred(int y, const char *str)
{
    int len = 0;
    for (const char *p = str; *p; p++) len++;
    int total_w = len * 6 - 1;  /* last char has no trailing gap */
    int x = (OLED_WIDTH - total_w) / 2;
    if (x < 0) x = 0;
    draw_string(x, y, str);
}

/* =========================
   MENU SCREENS
   ========================= */

/* Ground line shared across screens */
static void draw_ground(void)
{
    for (int x = 0; x < OLED_WIDTH; x++) {
        oled_draw_pixel(x, OLED_HEIGHT - 8);
    }
}

static void draw_main_menu(void)
{
    /* Title */
    draw_string_centred(4,  "DINO  RUN");
    draw_string_centred(14, "---------");

    /* Menu items */
    const char *items[MENU_ITEM_COUNT] = { "START", "HIGHSCORE" };

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int y = 26 + i * 12;

        if (i == (int)s_selected && s_blink) {
            /* Blinking cursor arrow */
            draw_char(8, y, '>');
        }

        draw_string(18, y, items[i]);
    }

    draw_ground();
}

static void draw_highscore_screen(void)
{
    char buf[24];

    draw_string_centred(4,  "HIGH SCORE");
    draw_string_centred(14, "----------");

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_highscore);
    draw_string_centred(28, buf);

    if (s_blink) {
        draw_string_centred(48, "PRESS BTN");
    }

    draw_ground();
}

static void draw_countdown_screen(void)
{
    draw_string_centred(20, "GET READY");

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", s_countdown);
    draw_string_centred(36, buf);

    draw_ground();
}

static void draw_game_over_screen(void)
{
    char buf[24];

    draw_string_centred(4, "GAME OVER");

    snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)s_last_score);
    draw_string_centred(18, buf);

    if (s_last_score >= s_highscore && s_last_score > 0) {
        draw_string_centred(30, "NEW BEST!");
    }

    if (s_blink) {
        draw_string_centred(44, "BTN RETRY");
    }

    draw_ground();
}

/* =========================
   PUBLIC API
   ========================= */

void menu_init(void)
{
    s_state        = MENU_STATE_MAIN;
    s_selected     = MENU_ITEM_START;
    s_countdown    = 3;
    s_blink        = true;
    s_blink_tick   = xTaskGetTickCount();
    s_countdown_tick = 0;
    s_last_dy      = 0;
    s_last_btn     = false;
}

void menu_input(int dy, bool btn)
{
    /* Rising-edge detection for button */
    bool btn_pressed = (btn && !s_last_btn);
    s_last_btn = btn;

    /* Edge detection for stick direction */
    bool dy_new = (dy != 0 && s_last_dy == 0);
    s_last_dy = dy;

    switch (s_state) {

    case MENU_STATE_MAIN:
        if (dy_new) {
            /* Wrap-around navigation */
            int next = ((int)s_selected + dy + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
            s_selected = (MainMenuItem)next;
        }
        if (btn_pressed) {
            if (s_selected == MENU_ITEM_START) {
                s_state      = MENU_STATE_COUNTDOWN;
                s_countdown  = 3;
                s_countdown_tick = xTaskGetTickCount();
            } else {
                s_state = MENU_STATE_HIGHSCORE;
            }
        }
        break;

    case MENU_STATE_HIGHSCORE:
        if (btn_pressed) {
            s_state    = MENU_STATE_MAIN;
            s_selected = MENU_ITEM_START;
        }
        break;

    case MENU_STATE_COUNTDOWN:
        /* No input accepted during countdown */
        break;

    case MENU_STATE_GAME:
        /* Game loop owns input while playing */
        break;

    case MENU_STATE_GAME_OVER:
        if (btn_pressed) {
            s_state      = MENU_STATE_COUNTDOWN;
            s_countdown  = 3;
            s_countdown_tick = xTaskGetTickCount();
        }
        break;
    }
}

void menu_draw(void)
{
    uint32_t now = xTaskGetTickCount();

    /* Update blink timer (~500 ms) */
    if ((now - s_blink_tick) >= pdMS_TO_TICKS(500)) {
        s_blink      = !s_blink;
        s_blink_tick = now;
    }

    /* Advance countdown (1 second per count) */
    if (s_state == MENU_STATE_COUNTDOWN) {
        if ((now - s_countdown_tick) >= pdMS_TO_TICKS(1000)) {
            s_countdown--;
            s_countdown_tick = now;

            if (s_countdown <= 0) {
                s_state = MENU_STATE_GAME;
                return;  /* Skip draw; game loop takes over next cycle */
            }
        }
    }

    oled_clear();

    switch (s_state) {
    case MENU_STATE_MAIN:        draw_main_menu();        break;
    case MENU_STATE_HIGHSCORE:   draw_highscore_screen(); break;
    case MENU_STATE_COUNTDOWN:   draw_countdown_screen(); break;
    case MENU_STATE_GAME_OVER:   draw_game_over_screen(); break;
    case MENU_STATE_GAME:        /* game loop draws */    break;
    }

    oled_update();
}

MenuState menu_get_state(void)
{
    return s_state;
}

void menu_notify_game_over(uint32_t final_score)
{
    s_last_score = final_score;

    if (final_score > s_highscore) {
        s_highscore = final_score;
    }

    s_state = MENU_STATE_GAME_OVER;
}

uint32_t menu_get_highscore(void)
{
    return s_highscore;
}