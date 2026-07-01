#include "snake.h"
#include "tft.h"
#include "font.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>

/* ─── color palette ──────────────────────────────────────────────────────── */
#define COL_BG        TFT_BLACK
#define COL_BORDER    TFT_DARKGRAY
#define COL_HUD_TEXT  TFT_WHITE
#define COL_HUD_LINE  TFT_DARKGRAY
#define COL_SNAKE_BODY TFT_GREEN
#define COL_SNAKE_HEAD TFT_DARKGREEN
#define COL_SNAKE_EYE  TFT_BLACK
#define COL_APPLE_BODY TFT_RED
#define COL_APPLE_STEM TFT_BROWN
#define COL_APPLE_LEAF TFT_DARKGREEN

/* ─── helpers ────────────────────────────────────────────────────────────── */

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static inline int to_px_x(int gx) { return gx * CELL_SIZE; }
static inline int to_px_y(int gy) { return HUD_H + gy * CELL_SIZE; }

static inline SnakeCell body_at(const SnakeGame *g, int pos)
{
    int idx = (g->head_idx - pos + SNAKE_MAX_LEN) % SNAKE_MAX_LEN;
    return g->body[idx];
}

/* ─── occupancy bitmap ───────────────────────────────────────────────────── */

static uint8_t s_occ[(GRID_W * GRID_H + 7) / 8];

static void occ_clear(void) { memset(s_occ, 0, sizeof(s_occ)); }

static void occ_set(int x, int y, bool v)
{
    int b = y * GRID_W + x;
    if (v) s_occ[b/8] |=  (1<<(b%8));
    else   s_occ[b/8] &= ~(1<<(b%8));
}

static bool occ_get(int x, int y)
{
    int b = y * GRID_W + x;
    return (s_occ[b/8] >> (b%8)) & 1;
}

static void rebuild_occ(const SnakeGame *g)
{
    occ_clear();
    for (int i = 0; i < g->length; i++) {
        SnakeCell c = body_at(g, i);
        occ_set(c.x, c.y, true);
    }
}

/* ─── food spawn ─────────────────────────────────────────────────────────── */

static void spawn_food(SnakeGame *g)
{
    int free = GRID_W * GRID_H - g->length;
    if (free <= 0) { g->status = SNAKE_WIN; return; }

    int target = (int)(esp_random() % (uint32_t)free), seen = 0;
    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            if (!occ_get(x, y) && seen++ == target)
                { g->food.x = x; g->food.y = y; return; }
}

/* ─── direction helpers ──────────────────────────────────────────────────── */

static bool is_opposite(Direction a, Direction b)
{
    return (a==DIR_UP&&b==DIR_DOWN)||(a==DIR_DOWN&&b==DIR_UP)||
           (a==DIR_LEFT&&b==DIR_RIGHT)||(a==DIR_RIGHT&&b==DIR_LEFT);
}

static void dir_delta(Direction d, int *dx, int *dy)
{
    *dx=0; *dy=0;
    switch(d){
        case DIR_UP:    *dy=-1; break;
        case DIR_DOWN:  *dy= 1; break;
        case DIR_LEFT:  *dx=-1; break;
        case DIR_RIGHT: *dx= 1; break;
    }
}

/* ─── neighbour bitmask for connected body rendering ─────────────────────── */

#define NBR_UP    (1<<0)
#define NBR_DOWN  (1<<1)
#define NBR_LEFT  (1<<2)
#define NBR_RIGHT (1<<3)

static uint8_t nbr_mask(const SnakeGame *g, int pos)
{
    SnakeCell cur = body_at(g, pos);
    uint8_t m = 0;
    int dirs[2] = { pos-1, pos+1 };
    int limits[2] = { 0, g->length-1 };
    for (int d = 0; d < 2; d++) {
        int p = dirs[d];
        if (p < 0 || p > limits[1]) continue;
        SnakeCell nb = body_at(g, p);
        int dx = nb.x - cur.x, dy = nb.y - cur.y;
        if (dx ==  1) m |= NBR_RIGHT;
        if (dx == -1) m |= NBR_LEFT;
        if (dy ==  1) m |= NBR_DOWN;
        if (dy == -1) m |= NBR_UP;
    }
    return m;
}

/*
 * Body segment — filled solid green, flush on neighbour sides, 1px inset
 * on free sides (same connectivity logic as before, now with color fill
 * instead of a monochrome outline — a solid color body reads far more
 * clearly as "one continuous creature" than the old hollow-outline look
 * the 1-bit display required).
 */
static void draw_segment(int gx, int gy, uint8_t nbr)
{
    int px = to_px_x(gx), py = to_px_y(gy);
    int x0 = px + ((nbr & NBR_LEFT)  ? 0 : 1);
    int y0 = py + ((nbr & NBR_UP)    ? 0 : 1);
    int x1 = px + CELL_SIZE - 1 - ((nbr & NBR_RIGHT) ? 0 : 1);
    int y1 = py + CELL_SIZE - 1 - ((nbr & NBR_DOWN)  ? 0 : 1);
    tft_fill_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, COL_SNAKE_BODY);
}

/*
 * Head — darker green fill (visually distinct from the body color) with
 * two black "eye" pixels placed at the leading corners based on direction.
 */
static void draw_head(int gx, int gy, Direction dir, uint8_t nbr)
{
    int px = to_px_x(gx), py = to_px_y(gy);
    int x0 = px + ((nbr & NBR_LEFT)  ? 0 : 1);
    int y0 = py + ((nbr & NBR_UP)    ? 0 : 1);
    int x1 = px + CELL_SIZE - 1 - ((nbr & NBR_RIGHT) ? 0 : 1);
    int y1 = py + CELL_SIZE - 1 - ((nbr & NBR_DOWN)  ? 0 : 1);

    tft_fill_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, COL_SNAKE_HEAD);

    /* Eye positions relative to cell top-left, scaled for CELL_SIZE=8 */
    int e1x, e1y, e2x, e2y;
    switch (dir) {
        case DIR_RIGHT: e1x=CELL_SIZE-3; e1y=2;          e2x=CELL_SIZE-3; e2y=CELL_SIZE-3; break;
        case DIR_LEFT:  e1x=2;           e1y=2;          e2x=2;           e2y=CELL_SIZE-3; break;
        case DIR_UP:    e1x=2;           e1y=2;          e2x=CELL_SIZE-3; e2y=2;           break;
        case DIR_DOWN:  e1x=2;           e1y=CELL_SIZE-3;e2x=CELL_SIZE-3; e2y=CELL_SIZE-3; break;
        default:        e1x=-1;e1y=-1;   e2x=-1;e2y=-1; break;
    }
    tft_draw_pixel(px + e1x, py + e1y, COL_SNAKE_EYE);
    tft_draw_pixel(px + e2x, py + e2y, COL_SNAKE_EYE);
}

/*
 * Apple — colored round body (red), brown stem, dark green leaf. The
 * shape itself is identical to the old monochrome version, but having
 * red flesh + brown stem + green leaf makes it instantly read as "apple"
 * the way a single-color silhouette never quite could.
 */
static void draw_apple(int gx, int gy)
{
    int cx = to_px_x(gx) + CELL_SIZE/2;
    int cy = to_px_y(gy) + CELL_SIZE/2;

    tft_draw_pixel(cx,   cy-3, COL_APPLE_STEM);
    tft_draw_pixel(cx-1, cy-2, COL_APPLE_LEAF);

    for (int dx=-2;dx<=2;dx++) tft_draw_pixel(cx+dx, cy-1, COL_APPLE_BODY);
    for (int dx=-2;dx<=2;dx++) tft_draw_pixel(cx+dx, cy,   COL_APPLE_BODY);
    for (int dx=-2;dx<=2;dx++) tft_draw_pixel(cx+dx, cy+1, COL_APPLE_BODY);
    for (int dx=-1;dx<=1;dx++) tft_draw_pixel(cx+dx, cy+2, COL_APPLE_BODY);
    tft_draw_pixel(cx, cy+3, COL_APPLE_BODY);
}

/* ─── HUD score strip ────────────────────────────────────────────────────── */

static void draw_hud(const SnakeGame *g)
{
    tft_fill_rect(0, 0, TFT_WIDTH, HUD_H, COL_BG);

    char buf[16];
    snprintf(buf, sizeof(buf), "SCORE: %lu", (unsigned long)g->score);
    font_draw_str(2, 4, buf, COL_HUD_TEXT);

    for (int x = 0; x < TFT_WIDTH; x++) tft_draw_pixel(x, HUD_H - 1, COL_HUD_LINE);
}

/* ─── border (play area only, below HUD) ─────────────────────────────────── */

static void draw_border(void)
{
    for (int x = 0; x < TFT_WIDTH; x++) tft_draw_pixel(x, TFT_HEIGHT - 1, COL_BORDER);
    for (int y = HUD_H; y < TFT_HEIGHT; y++) {
        tft_draw_pixel(0,             y, COL_BORDER);
        tft_draw_pixel(TFT_WIDTH - 1, y, COL_BORDER);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   PUBLIC API
   ═══════════════════════════════════════════════════════════════════════════ */

void snake_init(SnakeGame *g)
{
    memset(g, 0, sizeof(SnakeGame));
    g->dir          = DIR_RIGHT;
    g->next_dir     = DIR_RIGHT;
    g->length       = SNAKE_START_LEN;
    g->step_ms      = SPEED_INITIAL_MS;
    g->status       = SNAKE_ALIVE;
    g->head_idx     = SNAKE_START_LEN - 1;
    g->food_visible = true;
    g->food_blink_ms = now_ms();

    for (int i = 0; i < SNAKE_START_LEN; i++) {
        g->body[i].x = (int8_t)(SNAKE_START_X - (SNAKE_START_LEN-1-i));
        g->body[i].y = (int8_t)SNAKE_START_Y;
    }
    rebuild_occ(g);
    spawn_food(g);
}

void snake_input(SnakeGame *g, int dx, int dy)
{
    Direction w = g->dir;
    if      (dx < 0) w = DIR_LEFT;
    else if (dx > 0) w = DIR_RIGHT;
    else if (dy < 0) w = DIR_UP;
    else if (dy > 0) w = DIR_DOWN;
    if (!is_opposite(w, g->dir)) g->next_dir = w;
}

SnakeStatus snake_tick(SnakeGame *g)
{
    if (g->status != SNAKE_ALIVE) return g->status;

    g->dir = g->next_dir;
    int dx, dy;
    dir_delta(g->dir, &dx, &dy);

    SnakeCell old_head = body_at(g, 0);
    int nx = old_head.x + dx;
    int ny = old_head.y + dy;

    /* Wall */
    if (nx<0||nx>=GRID_W||ny<0||ny>=GRID_H) {
        g->status = SNAKE_DEAD_WALL;
        static const Note death_tune[] = {
            { NOTE_A4, 90 }, { NOTE_F4, 90 }, { NOTE_D4, 90 }, { NOTE_C4, 280 },
        };
        sound_play_melody(death_tune, sizeof(death_tune)/sizeof(death_tune[0]));
        return g->status;
    }

    /* Self — vacate tail first */
    SnakeCell tail = body_at(g, g->length-1);
    occ_set(tail.x, tail.y, false);
    if (occ_get(nx, ny)) {
        g->status = SNAKE_DEAD_SELF;
        static const Note death_tune[] = {
            { NOTE_A4, 90 }, { NOTE_F4, 90 }, { NOTE_D4, 90 }, { NOTE_C4, 280 },
        };
        sound_play_melody(death_tune, sizeof(death_tune)/sizeof(death_tune[0]));
        return g->status;
    }

    /* Move */
    g->head_idx = (g->head_idx+1) % SNAKE_MAX_LEN;
    g->body[g->head_idx].x = (int8_t)nx;
    g->body[g->head_idx].y = (int8_t)ny;
    occ_set(nx, ny, true);

    /* Food */
    if (nx == g->food.x && ny == g->food.y) {
        g->length++;
        g->score += 10;
        g->food_eaten++;
        occ_set(tail.x, tail.y, true);

        static const Note eat_tune[] = { { NOTE_G4, 35 }, { NOTE_C5, 50 } };
        sound_play_melody(eat_tune, sizeof(eat_tune)/sizeof(eat_tune[0]));

        if ((g->food_eaten % SPEED_FOOD_INTERVAL) == 0) {
            g->step_ms -= SPEED_STEP_MS;
            if (g->step_ms < SPEED_MIN_MS) g->step_ms = SPEED_MIN_MS;
        }
        rebuild_occ(g);
        spawn_food(g);
    }
    return g->status;
}

void snake_draw(const SnakeGame *g)
{
    int64_t t = now_ms();
    if ((t - g->food_blink_ms) >= FOOD_BLINK_MS) {
        SnakeGame *gm = (SnakeGame *)g;
        gm->food_visible  = !gm->food_visible;
        gm->food_blink_ms = t;
    }

    tft_fill_rect(0, HUD_H, TFT_WIDTH, TFT_HEIGHT - HUD_H, COL_BG);
    draw_hud(g);
    draw_border();

    for (int i = g->length-1; i >= 1; i--) {
        SnakeCell c = body_at(g, i);
        draw_segment(c.x, c.y, nbr_mask(g, i));
    }

    SnakeCell h = body_at(g, 0);
    draw_head(h.x, h.y, g->dir, nbr_mask(g, 0));

    if (g->food_visible) draw_apple(g->food.x, g->food.y);
}

uint32_t snake_get_score(const SnakeGame *g)  { return g->score;   }
int      snake_get_step_ms(const SnakeGame *g) { return g->step_ms; }