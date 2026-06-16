#include "snake.h"
#include "oled.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

/* ─── font (5×7, ASCII 0x20-0x7E, columns LSB=top) ─────────────────────── */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00},
    {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
    {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41},
    {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E},
    {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00},
    {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00},
    {0x20,0x40,0x44,0x3D,0x00}, {0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C},
    {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x40,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00}, {0x00,0x41,0x36,0x08,0x00},
    {0x10,0x08,0x08,0x10,0x08},
};

/* ─── helpers ────────────────────────────────────────────────────────────── */

static inline int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* Grid coord → pixel origin, offset by HUD strip */
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

/* ─── font draw (used for HUD score) ────────────────────────────────────── */

static void draw_char_at(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *g = font5x7[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 8; row++)
            if (bits & (1<<row)) oled_draw_pixel(x+col, y+row);
    }
}

static void draw_str(int x, int y, const char *s)
{
    while (*s) { draw_char_at(x, y, *s++); x += 6; }
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
    int dirs[2] = { pos-1, pos+1 };   /* toward head, toward tail */
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
 * Body segment — flush on neighbour sides, 1px inset on free sides.
 * This makes adjacent cells share an edge so the snake looks continuous.
 */
static void draw_segment(int gx, int gy, uint8_t nbr)
{
    int px = to_px_x(gx), py = to_px_y(gy);
    int x0 = px + ((nbr & NBR_LEFT)  ? 0 : 1);
    int y0 = py + ((nbr & NBR_UP)    ? 0 : 1);
    int x1 = px + CELL_SIZE - 1 - ((nbr & NBR_RIGHT) ? 0 : 1);
    int y1 = py + CELL_SIZE - 1 - ((nbr & NBR_DOWN)  ? 0 : 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            oled_draw_pixel(x, y);
}

/*
 * Head — same inset logic as body but with two "eye" pixels left dark.
 * Eyes are placed at the leading corners based on movement direction.
 */
static void draw_head(int gx, int gy, Direction dir, uint8_t nbr)
{
    int px = to_px_x(gx), py = to_px_y(gy);
    int x0 = px + ((nbr & NBR_LEFT)  ? 0 : 1);
    int y0 = py + ((nbr & NBR_UP)    ? 0 : 1);
    int x1 = px + CELL_SIZE - 1 - ((nbr & NBR_RIGHT) ? 0 : 1);
    int y1 = py + CELL_SIZE - 1 - ((nbr & NBR_DOWN)  ? 0 : 1);

    /* Eye local coords (relative to px,py) */
    int e1x, e1y, e2x, e2y;
    switch (dir) {
        case DIR_RIGHT: e1x=CELL_SIZE-2; e1y=1;          e2x=CELL_SIZE-2; e2y=CELL_SIZE-2; break;
        case DIR_LEFT:  e1x=1;           e1y=1;          e2x=1;           e2y=CELL_SIZE-2; break;
        case DIR_UP:    e1x=1;           e1y=1;          e2x=CELL_SIZE-2; e2y=1;           break;
        case DIR_DOWN:  e1x=1;           e1y=CELL_SIZE-2;e2x=CELL_SIZE-2; e2y=CELL_SIZE-2; break;
        default:        e1x=-1;e1y=-1;   e2x=-1;e2y=-1; break;
    }

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            int lx = x-px, ly = y-py;
            if ((lx==e1x&&ly==e1y)||(lx==e2x&&ly==e2y)) continue;
            oled_draw_pixel(x, y);
        }
    }
}

/*
 * Apple — round body with stem and leaf, drawn centred on the grid cell.
 * At CELL_SIZE=6 the centre is at offset (3,3) from cell top-left.
 *
 *   Pixel pattern (relative to centre cx,cy):
 *        X          cy-3  stem
 *       X           cy-2  leaf (left of stem)
 *      XXXXX        cy-1  body top
 *      XXXXX        cy    body mid
 *      XXXXX        cy+1  body bot
 *       XXX         cy+2  rounded bottom
 *        X          cy+3  tip
 */
static void draw_apple(int gx, int gy)
{
    int cx = to_px_x(gx) + CELL_SIZE/2;
    int cy = to_px_y(gy) + CELL_SIZE/2;

    oled_draw_pixel(cx,     cy-3);          /* stem  */
    oled_draw_pixel(cx-1,   cy-2);          /* leaf  */
    for (int dx=-2;dx<=2;dx++) oled_draw_pixel(cx+dx, cy-1); /* body  */
    for (int dx=-2;dx<=2;dx++) oled_draw_pixel(cx+dx, cy  );
    for (int dx=-2;dx<=2;dx++) oled_draw_pixel(cx+dx, cy+1);
    for (int dx=-1;dx<=1;dx++) oled_draw_pixel(cx+dx, cy+2); /* round */
    oled_draw_pixel(cx,     cy+3);          /* tip   */
}

/* ─── HUD score strip ────────────────────────────────────────────────────── */

static void draw_hud(const SnakeGame *g)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "SCR:%lu", (unsigned long)g->score);
    draw_str(1, 1, buf);

    /* Thin separator line below HUD */
    for (int x = 0; x < 128; x++) oled_draw_pixel(x, HUD_H - 1);
}

/* ─── border (play area only, below HUD) ─────────────────────────────────── */

static void draw_border(void)
{
    /* Top of play area = HUD separator line, already drawn */
    for (int x = 0; x < 128; x++) oled_draw_pixel(x, 63);   /* bottom */
    for (int y = HUD_H; y < 64; y++) {
        oled_draw_pixel(0,   y);   /* left  */
        oled_draw_pixel(127, y);   /* right */
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
    if (nx<0||nx>=GRID_W||ny<0||ny>=GRID_H)
        { g->status=SNAKE_DEAD_WALL; return g->status; }

    /* Self — vacate tail first */
    SnakeCell tail = body_at(g, g->length-1);
    occ_set(tail.x, tail.y, false);
    if (occ_get(nx, ny)) { g->status=SNAKE_DEAD_SELF; return g->status; }

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
        occ_set(tail.x, tail.y, true);   /* tail didn't slide */

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
    /* Real-time food blink */
    int64_t t = now_ms();
    if ((t - g->food_blink_ms) >= FOOD_BLINK_MS) {
        SnakeGame *gm = (SnakeGame *)g;
        gm->food_visible  = !gm->food_visible;
        gm->food_blink_ms = t;
    }

    draw_hud(g);
    draw_border();

    /* Body — tail to neck */
    for (int i = g->length-1; i >= 1; i--) {
        SnakeCell c = body_at(g, i);
        draw_segment(c.x, c.y, nbr_mask(g, i));
    }

    /* Head */
    SnakeCell h = body_at(g, 0);
    draw_head(h.x, h.y, g->dir, nbr_mask(g, 0));

    /* Apple */
    if (g->food_visible) draw_apple(g->food.x, g->food.y);
}

uint32_t snake_get_score(const SnakeGame *g)  { return g->score;   }
int      snake_get_step_ms(const SnakeGame *g) { return g->step_ms; }