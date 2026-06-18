/*
 * MAZE / DUNGEON — ESP32 OLED console game
 *
 * Algorithm: Recursive backtracker (iterative with explicit stack to avoid
 * C call-stack overflow on ESP32 where task stacks are limited).
 *
 * Grid: 13×7 cells (each cell = 8×8 pixels, walls on shared edges)
 *   Display area: 104×56 pixels leaving a 12px HUD strip at top.
 *   Wall between cells = 1 pixel; cell interior = 6×6 pixels.
 *
 * Memory:
 *   Walls stored as two bitfields per cell:
 *     h_walls[row][col] = 1 if wall exists on RIGHT edge of cell(row,col)
 *     v_walls[row][col] = 1 if wall exists on BOTTOM edge of cell(row,col)
 *   8 bytes each → 112 bytes total for a 13×7 maze.
 *
 *   Visited/coin flags: 1 bit each → 2 × 12 bytes = 24 bytes.
 *
 * Fog of war: cells within Manhattan distance 2 of player are revealed.
 * Revealed state is persistent (cells stay visible once seen).
 *
 * Layout:
 *   y=0..8    HUD (score, timer, level)
 *   y=9       separator
 *   y=10..63  maze play area (54px = 7 cells × (6px interior + 1px wall) + 1)
 *   x=0..103  maze (13 cells × 8px = 104px, centred → x_offset=12)
 */

#include "maze.h"
#include "oled.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <string.h>
#include <stdio.h>

/* ─── grid dimensions ────────────────────────────────────────────────────── */
#define MZ_COLS     13
#define MZ_ROWS      7
#define CELL_PX      8      /* pixels per cell (including shared wall)       */
#define WALL_PX      1      /* wall thickness in pixels                      */
#define INNER_PX     6      /* cell interior size (CELL_PX - WALL_PX - 1)   */

#define MZ_X_OFF    12      /* pixel x of maze left edge                     */
#define MZ_Y_OFF    10      /* pixel y of maze top edge                      */

/* ─── derived pixel helpers ──────────────────────────────────────────────── */
/* Top-left pixel of cell interior */
#define CELL_PX_X(c)  (MZ_X_OFF + (c) * CELL_PX + WALL_PX)
#define CELL_PX_Y(r)  (MZ_Y_OFF + (r) * CELL_PX + WALL_PX)

/* ─── game tuning ────────────────────────────────────────────────────────── */
#define FOG_RADIUS      2       /* cells revealed around player             */
#define COIN_SCORE      20      /* points per coin                          */
#define EXIT_SCORE      100     /* points for reaching exit                 */
#define TIME_BONUS_MAX  200     /* max time bonus (decreases per second)    */
#define TIME_BONUS_RATE 5       /* bonus lost per second                    */
#define TICK_MS         20

/* ─── wall storage ───────────────────────────────────────────────────────── */
/*
 * h_walls[r][c] — wall on RIGHT side of cell (r,c)  (horizontal edge)
 * v_walls[r][c] — wall on BOTTOM side of cell (r,c) (vertical edge)
 * Stored as bytes (1 bit per column), packed into uint16 per row.
 * MZ_COLS=13 fits in uint16 (max 16 bits).
 */

/* Actually store per-cell as full uint8 arrays for clarity on ESP32 */
static uint8_t rwall[MZ_ROWS][MZ_COLS];   /* 1 = wall on right  */
static uint8_t bwall[MZ_ROWS][MZ_COLS];   /* 1 = wall on bottom */
static uint8_t vis[MZ_ROWS][MZ_COLS];     /* generation visited  */
static uint8_t fog[MZ_ROWS][MZ_COLS];     /* fog of war: 1=seen  */
static uint8_t coin[MZ_ROWS][MZ_COLS];    /* 1 = coin present    */

/* ─── font ───────────────────────────────────────────────────────────────── */
static const uint8_t mz_font[][5] = {
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

static void mz_char(int x, int y, char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *g = mz_font[c - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t b = g[col];
        for (int row = 0; row < 8; row++)
            if (b & (1 << row)) oled_draw_pixel(x + col, y + row);
    }
}
static void mz_str(int x, int y, const char *s)
{ while (*s) { mz_char(x, y, *s++); x += 6; } }

/* ─── maze generation — iterative DFS (recursive backtracker) ────────────── */

/*
 * Direction encoding:
 *   0 = right  (+col)
 *   1 = down   (+row)
 *   2 = left   (-col)
 *   3 = up     (-row)
 */
static const int DR[4] = { 0,  1,  0, -1};
static const int DC[4] = { 1,  0, -1,  0};

static void maze_generate(void)
{
    /* DFS stack — local to avoid polluting file scope */
    #define STACK_MAX (MZ_COLS * MZ_ROWS)
    uint8_t gen_stack_r[STACK_MAX];
    uint8_t gen_stack_c[STACK_MAX];

    /* Clear all arrays */
    memset(rwall, 1, sizeof(rwall));   /* start with all walls present */
    memset(bwall, 1, sizeof(bwall));
    memset(vis,   0, sizeof(vis));
    memset(fog,   0, sizeof(fog));
    memset(coin,  0, sizeof(coin));

    /* Remove outer boundary ambiguity — border walls are always solid */

    /* Iterative DFS stack */
    int sp = 0;
    gen_stack_r[sp] = 0;
    gen_stack_c[sp] = 0;
    vis[0][0] = 1;

    while (sp >= 0) {
        int r = gen_stack_r[sp];
        int c = gen_stack_c[sp];

        /* Shuffle directions using Fisher-Yates on 4 elements */
        int dirs[4] = {0, 1, 2, 3};
        for (int i = 3; i > 0; i--) {
            int j = (int)(esp_random() % (uint32_t)(i + 1));
            int tmp = dirs[i]; dirs[i] = dirs[j]; dirs[j] = tmp;
        }

        bool pushed = false;
        for (int di = 0; di < 4; di++) {
            int d  = dirs[di];
            int nr = r + DR[d];
            int nc = c + DC[d];
            if (nr < 0 || nr >= MZ_ROWS || nc < 0 || nc >= MZ_COLS) continue;
            if (vis[nr][nc]) continue;

            /* Remove wall between (r,c) and (nr,nc) */
            if (d == 0) rwall[r][c]  = 0;   /* remove right wall of (r,c)  */
            if (d == 1) bwall[r][c]  = 0;   /* remove bottom wall of (r,c) */
            if (d == 2) rwall[r][nc] = 0;   /* remove right wall of (r,nc) */
            if (d == 3) bwall[nr][c] = 0;   /* remove bottom wall of (nr,c)*/

            vis[nr][nc] = 1;
            sp++;
            gen_stack_r[sp] = (uint8_t)nr;
            gen_stack_c[sp] = (uint8_t)nc;
            pushed = true;
            break;
        }

        if (!pushed) sp--;   /* backtrack */
    }

    /*
     * Place coins in dead-ends (cells with only one open passage).
     * Dead-ends are natural rest points the player reaches — rewarding
     * thorough exploration.
     */
    for (int r = 0; r < MZ_ROWS; r++) {
        for (int c = 0; c < MZ_COLS; c++) {
            /* Skip start and exit cells */
            if (r == 0 && c == 0) continue;
            if (r == MZ_ROWS-1 && c == MZ_COLS-1) continue;

            int open = 0;
            if (c < MZ_COLS-1 && !rwall[r][c])   open++;  /* right open */
            if (c > 0          && !rwall[r][c-1]) open++;  /* left open  */
            if (r < MZ_ROWS-1  && !bwall[r][c])   open++;  /* down open  */
            if (r > 0          && !bwall[r-1][c]) open++;  /* up open    */

            if (open == 1) coin[r][c] = 1;   /* dead-end — place coin */
        }
    }
}

/* ─── fog of war update ──────────────────────────────────────────────────── */
static void update_fog(int pr, int pc)
{
    for (int r = 0; r < MZ_ROWS; r++) {
        for (int c = 0; c < MZ_COLS; c++) {
            int dist = (r - pr < 0 ? pr - r : r - pr) +
                       (c - pc < 0 ? pc - c : c - pc);
            if (dist <= FOG_RADIUS) fog[r][c] = 1;
        }
    }
}

/* ─── game state ─────────────────────────────────────────────────────────── */
static struct {
    int      pr, pc;        /* player row, col                               */
    uint32_t score;
    int      level;
    int      time_bonus;
    bool     game_over;     /* true when player exits the maze               */

    /* Input debounce — only move one cell per joystick push */
    int      last_dx, last_dy;

    int64_t  last_tick_ms;
    int64_t  last_second_ms;   /* for time bonus countdown                   */
} MZ;

static inline int64_t mz_now(void) { return esp_timer_get_time() / 1000; }

/* ─── public API ─────────────────────────────────────────────────────────── */

void maze_init(void)
{
    memset(&MZ, 0, sizeof(MZ));
    MZ.level        = 1;
    MZ.time_bonus   = TIME_BONUS_MAX;
    MZ.last_tick_ms = mz_now();
    MZ.last_second_ms = mz_now();
    maze_generate();
    update_fog(0, 0);
}

void maze_input(int dx, int dy, bool btn)
{
    (void)btn;

    /* Only act on new joystick push (edge detection) */
    bool dx_new = (dx != 0 && MZ.last_dx == 0);
    bool dy_new = (dy != 0 && MZ.last_dy == 0);
    MZ.last_dx = dx;
    MZ.last_dy = dy;

    if (!dx_new && !dy_new) return;

    int nr = MZ.pr, nc = MZ.pc;

    /* Determine attempted move direction */
    int move_dir = -1;
    if      (dx_new && dx >  0) move_dir = 0;  /* right */
    else if (dy_new && dy >  0) move_dir = 1;  /* down  */
    else if (dx_new && dx <  0) move_dir = 2;  /* left  */
    else if (dy_new && dy <  0) move_dir = 3;  /* up    */

    if (move_dir < 0) return;

    /* Check wall */
    bool blocked = false;
    switch (move_dir) {
        case 0: if (nc >= MZ_COLS-1 || rwall[nr][nc])   blocked = true; else nc++; break;
        case 1: if (nr >= MZ_ROWS-1 || bwall[nr][nc])   blocked = true; else nr++; break;
        case 2: if (nc <= 0         || rwall[nr][nc-1]) blocked = true; else nc--; break;
        case 3: if (nr <= 0         || bwall[nr-1][nc]) blocked = true; else nr--; break;
    }

    if (blocked) return;

    MZ.pr = nr;
    MZ.pc = nc;
    update_fog(nr, nc);

    /* Collect coin */
    if (coin[nr][nc]) {
        coin[nr][nc] = 0;
        MZ.score += COIN_SCORE;
    }

    /* Check exit */
    if (nr == MZ_ROWS - 1 && nc == MZ_COLS - 1) {
        MZ.score += EXIT_SCORE + (uint32_t)MZ.time_bonus;
        MZ.level++;
        MZ.time_bonus = TIME_BONUS_MAX;
        maze_generate();
        MZ.pr = 0; MZ.pc = 0;
        update_fog(0, 0);
    }
}

bool maze_tick(uint32_t *score_out)
{
    int64_t now = mz_now();
    if ((now - MZ.last_tick_ms) < TICK_MS) { *score_out = MZ.score; return true; }
    MZ.last_tick_ms = now;

    /* Countdown time bonus every second */
    if ((now - MZ.last_second_ms) >= 1000) {
        MZ.last_second_ms = now;
        MZ.time_bonus -= TIME_BONUS_RATE;
        if (MZ.time_bonus < 0) MZ.time_bonus = 0;
    }

    *score_out = MZ.score;
    return true;   /* maze never ends — player keeps solving levels */
}

void maze_draw(void)
{
    /* HUD */
    char buf[24];
    snprintf(buf, sizeof(buf), "SCR:%lu LVL:%d BNS:%d",
             (unsigned long)MZ.score, MZ.level, MZ.time_bonus);
    mz_str(1, 1, buf);
    for (int x = 0; x < 128; x++) oled_draw_pixel(x, 9);

    /*
     * Draw maze cells.
     * For each revealed cell draw:
     *   - Interior (blank — already cleared by oled_clear)
     *   - Right wall if rwall[r][c]
     *   - Bottom wall if bwall[r][c]
     * Border walls: top/left always drawn; bottom/right of grid = outer border.
     *
     * Unrevealed cells are drawn as solid filled squares (fog of war).
     */

    /* Outer top border */
    for (int c = 0; c < MZ_COLS; c++) {
        int px = MZ_X_OFF + c * CELL_PX;
        oled_draw_pixel(px, MZ_Y_OFF);
    }
    /* Outer left border */
    for (int r = 0; r < MZ_ROWS; r++) {
        int py = MZ_Y_OFF + r * CELL_PX;
        oled_draw_pixel(MZ_X_OFF, py);
    }

    for (int r = 0; r < MZ_ROWS; r++) {
        for (int c = 0; c < MZ_COLS; c++) {
            int px = MZ_X_OFF + c * CELL_PX;
            int py = MZ_Y_OFF + r * CELL_PX;

            if (!fog[r][c]) {
                /* Fog — fill cell solid */
                for (int dy = 1; dy <= INNER_PX; dy++)
                    for (int dx = 1; dx <= INNER_PX; dx++)
                        oled_draw_pixel(px + dx, py + dy);
                /* Also draw surrounding walls solidly */
                if (rwall[r][c] || c == MZ_COLS-1)
                    for (int dy = 0; dy <= CELL_PX; dy++)
                        oled_draw_pixel(px + CELL_PX, py + dy);
                if (bwall[r][c] || r == MZ_ROWS-1)
                    for (int dx = 0; dx <= CELL_PX; dx++)
                        oled_draw_pixel(px + dx, py + CELL_PX);
                continue;
            }

            /* Revealed cell — draw walls only */
            /* Right wall */
            if (rwall[r][c] || c == MZ_COLS - 1) {
                for (int dy = 0; dy <= CELL_PX; dy++)
                    oled_draw_pixel(px + CELL_PX, py + dy);
            }
            /* Bottom wall */
            if (bwall[r][c] || r == MZ_ROWS - 1) {
                for (int dx = 0; dx <= CELL_PX; dx++)
                    oled_draw_pixel(px + dx, py + CELL_PX);
            }

            /* Coin — small dot in cell centre */
            if (coin[r][c]) {
                int cx = px + CELL_PX / 2;
                int cy = py + CELL_PX / 2;
                oled_draw_pixel(cx,   cy);
                oled_draw_pixel(cx+1, cy);
                oled_draw_pixel(cx,   cy+1);
                oled_draw_pixel(cx+1, cy+1);
            }
        }
    }

    /* Exit marker — 'E' drawn at bottom-right cell */
    {
        int ex = MZ_X_OFF + (MZ_COLS-1) * CELL_PX + 1;
        int ey = MZ_Y_OFF + (MZ_ROWS-1) * CELL_PX + 1;
        if (fog[MZ_ROWS-1][MZ_COLS-1])
            mz_char(ex, ey, 'E');
    }

    /* Player — filled 3×3 square centred in cell */
    {
        int px = MZ_X_OFF + MZ.pc * CELL_PX + CELL_PX/2 - 1;
        int py = MZ_Y_OFF + MZ.pr * CELL_PX + CELL_PX/2 - 1;
        for (int dy = 0; dy < 3; dy++)
            for (int dx = 0; dx < 3; dx++)
                oled_draw_pixel(px + dx, py + dy);
    }
}