#pragma once

#include "tft.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PALETTE — shared retro-arcade color scheme for the entire console.
 *
 * Replaces the ad-hoc TFT_* color constants scattered across console.c,
 * each game file, and splash.c with a single, intentional palette —
 * derived from a retro pixel-art "GAME ON" title screen reference image
 * (dark navy background, blue UI panels, gold collectibles, cyan
 * highlights). Every screen in the console should pull its colors from
 * here rather than picking TFT_CYAN/TFT_WHITE/etc. ad hoc per-file, so
 * the whole device reads as one cohesive aesthetic instead of six
 * differently-colored games bolted together.
 *
 * RGB565 values below were hand-converted from the reference hex codes
 * using the standard 5-6-5 bit truncation: R5 = hex_r >> 3, G6 = hex_g >> 2,
 * B5 = hex_b >> 3, packed as (R5<<11)|(G6<<5)|B5.
 */

/* ── core background tones ────────────────────────────────────────────── */
#define PAL_BG_DARK     0x0042   /* #050816 — primary background, near-black navy   */
#define PAL_BG_PANEL    0x08C6   /* #091B35 — secondary background / menu panels    */
#define PAL_OUTLINE     0x11ED   /* #163D68 — panel borders, dividers, dim outlines */

/* ── interactive / highlight blues ────────────────────────────────────── */
#define PAL_BLUE_MAIN   0x3D5F   /* #3AA8FF — menu selection, cursor, primary accent */
#define PAL_BLUE_BRIGHT 0x7EDF   /* #78D9FF — sparkles, stars, glow effects          */

/* ── text and accents ─────────────────────────────────────────────────── */
#define PAL_WHITE       0xF7BF   /* #F2F4F8 — primary text                          */
#define PAL_GOLD        0xF626   /* #F4C430 — collectibles, achievements, coins     */

/* ── semantic aliases — use these in game/menu code, not the raw names ── *
 * Keeping one layer of semantic naming on top of the raw palette means
 * if the palette itself ever changes, only this block needs updating —
 * call sites stay meaningful regardless ("PAL_TEXT_DIM" reads clearly at
 * a call site in a way a raw hex-derived name wouldn't).
 */
#define PAL_BACKGROUND      PAL_BG_DARK
#define PAL_PANEL_BG        PAL_BG_PANEL
#define PAL_BORDER          PAL_OUTLINE
#define PAL_SELECTED        PAL_BLUE_BRIGHT   /* selected menu item — bright cyan/blue */
#define PAL_UNSELECTED      PAL_WHITE
#define PAL_CURSOR          PAL_BLUE_MAIN
#define PAL_TEXT            PAL_WHITE
#define PAL_TEXT_DIM        PAL_OUTLINE
#define PAL_TITLE           PAL_BLUE_MAIN
#define PAL_COLLECTIBLE     PAL_GOLD
#define PAL_GLOW            PAL_BLUE_BRIGHT

/* ── greens ───────────────────────────────────────────── */
#define PAL_GREEN_DARK     0x0341   // #006B20 — pipe cap, dark vegetation
#define PAL_GREEN_MAIN     0x0502   // #00A040 — pipe body, grass strip
#define PAL_GREEN_LIGHT    0x6F6D   // #6AE36B — grass blade tips (keep)
#define PAL_GREEN_GRASS    0x2E27   // darker grass highlight (keep)

/* ── earth ────────────────────────────────────────────── */
#define PAL_FOREST         0x02E0   // #0A5C0A — distant hill silhouette
#define PAL_BROWN_DARK     0x5960   // #5C2E00 — soil texture clumps
#define PAL_BROWN          0x8A20   // #8B4513 — soil base

#define PAL_DANGER          0xF800   /* TFT_RED — kept distinct from the palette on
                                         purpose: danger/game-over should NOT blend
                                         into the blue family, it needs to visually
                                         interrupt the cohesive palette to register
                                         as "something bad happened" */
#define PAL_SUCCESS         PAL_GOLD /* "good outcome" reuses gold rather than green,
                                         keeping the whole success/collectible
                                         vocabulary in one consistent color family */
                                         

#ifdef __cplusplus
}
#endif