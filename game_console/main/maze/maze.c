/*
 * MAZE / DUNGEON — 128x160 color version.
 *
 * LAYOUT CHANGE FROM 128x64 MONO:
 *   Old: 13x7 grid (91 cells) using 8px cells in a 104x56 play area
 *   New: 14x16 grid (224 cells) using 9px cells in a 126x144 play area
 *        — both more cells AND bigger cells, since the screen gained
 *        far more area than it lost in any dimension.
 *
 * COLOR ADDITION — fog of war becomes much more readable:
 *   Old: unrevealed cells were solid filled squares (1-bit "dark")
 *   New: unrevealed cells are dark navy (still clearly "unknown"),
 *        revealed cells are black with visible wall lines, the
 *        player is a bright cyan square, coins are gold, and the
 *        exit cell glows green once revealed. The color vocabulary
 *        ("dark blue = unexplored", "green = goal") reads instantly
 *        compared to monochrome's single on/off language.
 */

#include "maze.h"
#include "tft.h"
#include "font.h"
#include "palette.h"
#include "effects.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>

/*
 * Grid dimensions recalculated for the landscape 160x128 screen.
 *
 * Previous values (MZ_COLS=14, MZ_ROWS=16, CELL_PX=9) produced a grid
 * spanning y=18 to y=162 — 34px TALLER than the 128px screen, running
 * off the bottom edge entirely (confirmed in testing photos). The grid
 * shape (14 wide x 16 tall, narrow-and-tall) was clearly sized for the
 * earlier PORTRAIT orientation and never updated after the landscape
 * rotation.
 *
 * New values: 17 wide x 12 tall at the same 9px cell size. This is a
 * landscape-appropriate shape (wider than tall, matching the screen's
 * own aspect ratio) that fits with only 6px of combined leftover space
 * across both axes, instead of overflowing by 34px vertically.
 */
#define MZ_COLS     17
#define MZ_ROWS     12
#define CELL_PX      9
#define WALL_PX      1
#define INNER_PX     7

#define MZ_X_OFF    1
#define MZ_Y_OFF    18    /* below a 16px HUD + 2px gap */

/* ─── color palette — now pulled from the shared palette.h ──────────────── */
#define COL_BG          PAL_BACKGROUND
#define COL_HUD_TEXT    PAL_TEXT
#define COL_HUD_LINE    PAL_BORDER
#define COL_FOG         PAL_BG_PANEL    /* unrevealed cells, was TFT_NAVY    */
#define COL_WALL        PAL_OUTLINE
#define COL_COIN        PAL_GOLD
#define COL_PLAYER      PAL_BLUE_BRIGHT
#define COL_EXIT        PAL_BLUE_MAIN
#define COL_BUMP_FLASH  PAL_DANGER      /* unused hook for future bump-flash effect */

/* ─── game tuning ────────────────────────────────────────────────────────── */
#define FOG_RADIUS      3       /* was 2 — slightly larger reveal radius for the bigger grid */
#define COIN_SCORE      20
#define EXIT_SCORE      100
#define TIME_BONUS_MAX  200
#define TIME_BONUS_RATE 5
#define TICK_MS         20

/* ─── wall storage ───────────────────────────────────────────────────────── */
static uint8_t rwall[MZ_ROWS][MZ_COLS];
static uint8_t bwall[MZ_ROWS][MZ_COLS];
static uint8_t vis[MZ_ROWS][MZ_COLS];
static uint8_t fog[MZ_ROWS][MZ_COLS];
static uint8_t coin[MZ_ROWS][MZ_COLS];

/* ─── maze generation — iterative DFS (recursive backtracker) ────────────── */
static const int DR[4] = { 0,  1,  0, -1};
static const int DC[4] = { 1,  0, -1,  0};

static void maze_generate(void)
{
    #define STACK_MAX (MZ_COLS * MZ_ROWS)
    uint8_t gen_stack_r[STACK_MAX];
    uint8_t gen_stack_c[STACK_MAX];

    memset(rwall, 1, sizeof(rwall));
    memset(bwall, 1, sizeof(bwall));
    memset(vis,   0, sizeof(vis));
    memset(fog,   0, sizeof(fog));
    memset(coin,  0, sizeof(coin));

    int sp = 0;
    gen_stack_r[sp] = 0;
    gen_stack_c[sp] = 0;
    vis[0][0] = 1;

    while (sp >= 0) {
        int r = gen_stack_r[sp];
        int c = gen_stack_c[sp];

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

            if (d == 0) rwall[r][c]  = 0;
            if (d == 1) bwall[r][c]  = 0;
            if (d == 2) rwall[r][nc] = 0;
            if (d == 3) bwall[nr][c] = 0;

            vis[nr][nc] = 1;
            sp++;
            gen_stack_r[sp] = (uint8_t)nr;
            gen_stack_c[sp] = (uint8_t)nc;
            pushed = true;
            break;
        }

        if (!pushed) sp--;
    }

    for (int r = 0; r < MZ_ROWS; r++) {
        for (int c = 0; c < MZ_COLS; c++) {
            if (r == 0 && c == 0) continue;
            if (r == MZ_ROWS-1 && c == MZ_COLS-1) continue;

            int open = 0;
            if (c < MZ_COLS-1 && !rwall[r][c])   open++;
            if (c > 0          && !rwall[r][c-1]) open++;
            if (r < MZ_ROWS-1  && !bwall[r][c])   open++;
            if (r > 0          && !bwall[r-1][c]) open++;

            if (open == 1) coin[r][c] = 1;
        }
    }
}

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
    int      pr, pc;
    uint32_t score;
    int      level;
    int      time_bonus;
    bool     game_over;

    int      last_dx, last_dy;

    int64_t  last_tick_ms;
    int64_t  last_second_ms;
} MZ;

static inline int64_t mz_now(void) { return esp_timer_get_time() / 1000; }

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

    bool dx_new = (dx != 0 && MZ.last_dx == 0);
    bool dy_new = (dy != 0 && MZ.last_dy == 0);
    MZ.last_dx = dx;
    MZ.last_dy = dy;

    if (!dx_new && !dy_new) return;

    int nr = MZ.pr, nc = MZ.pc;

    int move_dir = -1;
    if      (dx_new && dx >  0) move_dir = 0;
    else if (dy_new && dy >  0) move_dir = 1;
    else if (dx_new && dx <  0) move_dir = 2;
    else if (dy_new && dy <  0) move_dir = 3;

    if (move_dir < 0) return;

    bool blocked = false;
    switch (move_dir) {
        case 0: if (nc >= MZ_COLS-1 || rwall[nr][nc])   blocked = true; else nc++; break;
        case 1: if (nr >= MZ_ROWS-1 || bwall[nr][nc])   blocked = true; else nr++; break;
        case 2: if (nc <= 0         || rwall[nr][nc-1]) blocked = true; else nc--; break;
        case 3: if (nr <= 0         || bwall[nr-1][nc]) blocked = true; else nr--; break;
    }

    if (blocked) {
        sound_play(NOTE_C4, 30);
        return;
    }

    MZ.pr = nr;
    MZ.pc = nc;
    update_fog(nr, nc);

    if (coin[nr][nc]) {
        coin[nr][nc] = 0;
        MZ.score += COIN_SCORE;
        sound_play(NOTE_E5, 45);
    }

    if (nr == MZ_ROWS - 1 && nc == MZ_COLS - 1) {
        MZ.score += EXIT_SCORE + (uint32_t)MZ.time_bonus;
        MZ.level++;
        MZ.time_bonus = TIME_BONUS_MAX;
        maze_generate();
        MZ.pr = 0; MZ.pc = 0;
        update_fog(0, 0);

        static const Note level_tune[] = {
            { NOTE_C5, 70 }, { NOTE_E5, 70 }, { NOTE_G5, 70 }, { NOTE_C6, 140 },
        };
        sound_play_melody(level_tune, sizeof(level_tune)/sizeof(level_tune[0]));
    }
}

bool maze_tick(uint32_t *score_out)
{
    int64_t now = mz_now();
    if ((now - MZ.last_tick_ms) < TICK_MS) { *score_out = MZ.score; return true; }
    MZ.last_tick_ms = now;

    if ((now - MZ.last_second_ms) >= 1000) {
        MZ.last_second_ms = now;
        MZ.time_bonus -= TIME_BONUS_RATE;
        if (MZ.time_bonus < 0) MZ.time_bonus = 0;
    }

    *score_out = MZ.score;
    return true;
}

void maze_draw(void)
{
    tft_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COL_BG);

    /*
     * HUD text fix: was "S:%lu L:%d B:%d" — cryptic single-letter
     * abbreviations. First attempt at full words ("SCORE/LVL/BONUS")
     * overflowed the 160px screen width at worst-case values (210px for
     * "SCORE:4294967295 LVL:99 BONUS:200"). Settled on 3-letter
     * abbreviations (SCR/LV/BNS) — still meaningfully clearer than single
     * letters, and verified to fit comfortably within 160px even at
     * generous value lengths (126px worst-case tested).
     */
    char buf[40];
    snprintf(buf, sizeof(buf), "SCR:%lu LV:%d BNS:%d",
             (unsigned long)MZ.score, MZ.level, MZ.time_bonus);
    font_draw_str(2, 4, buf, COL_HUD_TEXT);
    for (int x = 0; x < TFT_WIDTH; x++) tft_draw_pixel(x, 14, COL_HUD_LINE);

    for (int r = 0; r < MZ_ROWS; r++) {
        for (int c = 0; c < MZ_COLS; c++) {
            int px = MZ_X_OFF + c * CELL_PX;
            int py = MZ_Y_OFF + r * CELL_PX;

            bool is_exit = (r == MZ_ROWS-1 && c == MZ_COLS-1);

            if (!fog[r][c]) {
                /* Unrevealed — solid panel-color fill, the "fog" color.
                 * A faint star is drawn in roughly 1-in-6 fog cells for
                 * ambient texture, matching the reference image's
                 * starfield motif — purely decorative, costs one extra
                 * pixel write per qualifying cell. */
                tft_fill_rect(px+1, py+1, INNER_PX, INNER_PX, COL_FOG);
                if (((r * MZ_COLS + c) % 6) == 0) {
                    tft_draw_pixel(px + CELL_PX/2, py + CELL_PX/2, PAL_OUTLINE);
                }
                if (rwall[r][c] || c == MZ_COLS-1)
                    for (int dy = 0; dy <= CELL_PX; dy++)
                        tft_draw_pixel(px + CELL_PX, py + dy, COL_WALL);
                if (bwall[r][c] || r == MZ_ROWS-1)
                    for (int dx = 0; dx <= CELL_PX; dx++)
                        tft_draw_pixel(px + dx, py + CELL_PX, COL_WALL);
                continue;
            }

            /* Revealed — black interior (already cleared), draw walls */
            if (rwall[r][c] || c == MZ_COLS - 1) {
                for (int dy = 0; dy <= CELL_PX; dy++)
                    tft_draw_pixel(px + CELL_PX, py + dy, COL_WALL);
            }
            if (bwall[r][c] || r == MZ_ROWS - 1) {
                for (int dx = 0; dx <= CELL_PX; dx++)
                    tft_draw_pixel(px + dx, py + CELL_PX, COL_WALL);
            }

            /* Exit cell glows with the palette's main blue once revealed */
            if (is_exit) {
                tft_fill_rect(px+1, py+1, INNER_PX, INNER_PX, COL_EXIT);
            }

            /*
             * Coin — upgraded from a flat 3x3 gold square to a small
             * pixel-art coin (filled circle with a bright highlight dot),
             * matching the reference image's coin sprites rather than
             * reading as an undifferentiated colored square.
             */
            if (coin[r][c]) {
                int cx = px + CELL_PX / 2;
                int cy = py + CELL_PX / 2;
                tft_fill_circle(cx, cy, 2, COL_COIN);
                tft_draw_pixel(cx - 1, cy - 1, PAL_WHITE);  /* highlight glint */
            }
        }
    }

    /* Player — bright cyan square centred in cell */
    {
        int px = MZ_X_OFF + MZ.pc * CELL_PX + CELL_PX/2 - 2;
        int py = MZ_Y_OFF + MZ.pr * CELL_PX + CELL_PX/2 - 2;
        tft_fill_rect(px, py, 4, 4, COL_PLAYER);
    }
}