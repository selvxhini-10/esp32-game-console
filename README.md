# ESP32 Handheld Game Console

A fully self-contained retro arcade handheld built on an ESP32 microcontroller, featuring a color TFT display, six original games, a cohesive pixel-art aesthetic, and a polished boot experience — all implemented in C with FreeRTOS on ESP-IDF v6.

---

## Table of Contents

- [Hardware](#hardware)
- [Software Architecture](#software-architecture)
- [Display & Graphics](#display--graphics)
- [Color Palette](#color-palette)
- [Input System](#input-system)
- [Audio System](#audio-system)
- [Console State Machine](#console-state-machine)
- [Boot Splash Animation](#boot-splash-animation)
- [Games](#games)
- [Persistence — NVS High Scores](#persistence--nvs-high-scores)
- [Visual Effects](#visual-effects)
- [Build & Flash](#build--flash)
- [Project Structure](#project-structure)
- [Design Decisions & Known Fixes](#design-decisions--known-fixes)

---

## Hardware

| Component | Part | Notes |
|---|---|---|
| Microcontroller | ESP32 (dual-core Xtensa LX6, 240 MHz) | CPU0: main game loop / UI. CPU1: audio queue |
| Display | 1.8" ST7735S TFT, 160×128, SPI | Landscape orientation (MADCTL configured) |
| Joystick | Analog thumbstick (2-axis + click) | ADC oneshot — X/Y read each frame |
| Buttons | 3× GPIO pushbuttons | GPIO26: pause menu. GPIO27: action/flap/shoot. Joystick click: confirm/back |
| Buzzer | Passive piezo buzzer | Driven by LEDC PWM peripheral on CPU1 |
| Persistence | ESP32 NVS (Non-Volatile Storage) | One key per game for high score |

### Wiring Summary

```
ST7735S TFT   →  ESP32
─────────────────────────
SCK           →  GPIO18 (SPI CLK)
SDA/MOSI      →  GPIO23 (SPI MOSI)
CS            →  GPIO5
DC            →  GPIO4
RST           →  GPIO2
BL            →  3.3V (backlight always on)

Joystick
────────
VRX           →  GPIO34 (ADC1_CH6)
VRY           →  GPIO35 (ADC1_CH7)
SW            →  GPIO32 (pull-up, active low)

Buttons
───────
Pause         →  GPIO26 (pull-up, active low)
Action        →  GPIO27 (pull-up, active low)

Buzzer
──────
+             →  GPIO25 (LEDC PWM)
−             →  GND
```

---

## Software Architecture

```
app_main.c
│
├── splash.c / splash_font.c     Boot animation (runs once)
│
└── console.c                    Top-level state machine
    ├── snake.c
    ├── pong.c
    ├── breakout.c
    ├── flappy.c
    ├── spaceinvaders.c
    └── maze.c
    
Shared modules (used by all of the above):
    tft.c          SPI display driver + framebuffer
    font.c         1x and 2x bitmap font renderer
    palette.h      Shared RGB565 color definitions
    effects.c      Starfield, heart sprites, ambient animations
    sound.c        Non-blocking PWM audio on CPU1 via FreeRTOS queue
    joystick.c     ADC oneshot read with deadzone
    buttons.c      GPIO debounced button read
    nvs_scores.c   NVS get/set wrappers for uint32_t high scores
```

### Key Architectural Constraints

**No dynamic allocation.** Every game uses statically allocated state structs. `malloc` is never called in game or UI code — stack and `.bss` only.

**Single main task.** The entire game loop runs on CPU0's main task. Each call to `console_tick()` from `app_main` completes one frame: input → tick → draw → `tft_update()`. No RTOS tasks are created for game logic.

**Non-blocking audio.** All sound calls enqueue a `Note` struct to a FreeRTOS queue. A dedicated task on CPU1 dequeues and drives the LEDC peripheral, so buzzer timing never blocks the game loop.

**Fixed timestep per game.** Each game module tracks its own `last_tick_ms` timestamp and skips the physics/logic update if less than its target interval has elapsed. Draw always runs regardless (so the display stays live during input polling).

---

## Display & Graphics

### Driver — `tft.c`

The ST7735S is driven over SPI using ESP-IDF's `spi_device_transmit`. The driver exposes:

```c
void tft_fill_rect(int x, int y, int w, int h, uint16_t color);
void tft_draw_pixel(int x, int y, uint16_t color);
void tft_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
void tft_draw_rect(int x, int y, int w, int h, uint16_t color);
void tft_update(void);   // flushes framebuffer over SPI
```

All drawing writes into a 160×128×2-byte RAM framebuffer. `tft_update()` sends the entire buffer in one SPI DMA burst — no partial updates, no per-pixel SPI transactions during draw.

**Coordinate system:** `(0,0)` is top-left, x increases right, y increases down. CASET/RASET window is set once at init for the full 160×128 landscape region.

### Font — `font.c`

A compact 5×7 pixel bitmap font stored as a 96-character table (ASCII 32–127), each character packed as 5 bytes (one byte per column, 7 rows used). Renderer functions:

```c
void font_draw_char(int x, int y, char c, uint16_t color);
void font_draw_str(int x, int y, const char *s, uint16_t color);
void font_draw_str_centred(int y, const char *s, uint16_t color, int width);
void font_draw_char_2x(int x, int y, char c, uint16_t color);
void font_draw_str_2x(int x, int y, const char *s, uint16_t color);
void font_draw_str_2x_centred(int y, const char *s, uint16_t color, int width);
```

At 2x scale each pixel becomes a 2×2 block. Advance is 6px/char at 1x and 11px/char at 2x (10px glyph + 1px gap).

---

## Color Palette

All colors are defined in `palette.h` as RGB565 constants, computed from reference hex codes using the standard 5-6-5 bit truncation: `(R>>3 << 11) | (G>>2 << 5) | (B>>3)`.

```c
/* Background tones */
PAL_BG_DARK     0x0042   // #050816 — near-black navy, primary background
PAL_BG_PANEL    0x08C6   // #091B35 — secondary panels / menus
PAL_OUTLINE     0x11ED   // #163D68 — borders, dividers, dim text

/* Blues */
PAL_BLUE_MAIN   0x3D5F   // #3AA8FF — cursor, selection, primary accent
PAL_BLUE_BRIGHT 0x7EDF   // #78D9FF — highlights, glow, sparkles

/* Text & accents */
PAL_WHITE       0xF7BF   // #F2F4F8 — primary text
PAL_GOLD        0xF626   // #F4C430 — collectibles, achievements, titles

/* Greens (Flappy Bird pipes, Snake, ground) */
PAL_GREEN_DARK  0x0341   // #006B20 — pipe caps, dark vegetation
PAL_GREEN_MAIN  0x0502   // #00A040 — pipe body, grass strip
PAL_GREEN_LIGHT 0x6F6D   // #6AE36B — grass blade tips

/* Earth (Flappy Bird ground) */
PAL_FOREST      0x02E0   // distant hill silhouette
PAL_BROWN       0x8A20   // #8B4513 — soil base
PAL_BROWN_DARK  0x5960   // soil texture clumps

/* Semantic / danger */
PAL_DANGER      0xF800   // TFT_RED — game over, collision
PAL_GOLD (reused as PAL_SUCCESS)
```

Every screen in the console uses these constants. Individual games never hardcode `TFT_CYAN` / `TFT_WHITE` / etc. directly — they alias from the palette, so a palette change propagates everywhere.

---

## Input System

### Joystick — `joystick.c`

Uses ESP-IDF's `adc_oneshot` API. Both axes are read every frame. A configurable deadzone (default ±300 out of 4095) maps the raw ADC value to `{-1, 0, +1}` per axis. The joystick click is a separate GPIO with software debounce.

### Buttons — `buttons.c`

Three GPIO inputs with internal pull-ups, all active-low. Each is debounced in software: a press is registered only when the GPIO reads low for two consecutive polls (~30ms apart). Rising-edge detection (button release) is used for menu navigation to prevent repeat-fire.

**Button roles:**

| Physical button | In-game role | Menu role |
|---|---|---|
| Joystick click | Confirm / back | Confirm selection |
| GPIO26 | Pause menu | — |
| GPIO27 | Action (flap, shoot, place) | — |

---

## Audio System

### `sound.c` — Non-blocking PWM audio

The buzzer is driven by ESP32's LEDC peripheral (LED Control, repurposed for arbitrary PWM frequency). A FreeRTOS queue (`xQueueCreate`) holds up to 8 `Note` structs:

```c
typedef struct { uint32_t freq_hz; uint32_t duration_ms; } Note;
```

A dedicated task pinned to CPU1 (`xTaskCreatePinnedToCore`) dequeues notes and calls `ledc_set_freq` + `vTaskDelay`. The main loop calls:

```c
void sound_play(uint32_t freq, uint32_t duration_ms);        // single note
void sound_play_melody(const Note *notes, int count);         // note array
void sound_punch(uint32_t freq, uint32_t duration_ms);        // immediate interrupt
```

All three are non-blocking — they enqueue and return immediately. This means a collision sound never causes a frame drop.

**Note constants** (defined in `sound.h`): `NOTE_C4` through `NOTE_C6` covering two octaves. Each is a `#define` for its frequency in Hz.

---

## Console State Machine

`console.c` implements a six-state machine:

```
CON_SELECTING  ──[confirm]──►  CON_COUNTDOWN  ──[0]──►  CON_PLAYING
                                                              │
                                                         [pause btn]
                                                              │
CON_HELP  ◄──[help]──  CON_PAUSED  ◄─────────────────────────┘
                             │
                         [quit]──►  CON_SELECTING
                                          ▲
CON_GAME_OVER  ──[main menu]─────────────┘
     │
  [retry]──►  CON_COUNTDOWN
```

### Game Registry

New games are registered by adding one `GameDesc` literal to `s_games[]`:

```c
typedef struct {
    const char *name;
    void (*init)(void);
    void (*input)(int dx, int dy, bool btn);
    bool (*tick)(uint32_t *score_out);    // returns false when game over
    void (*draw)(void);
    const char *help[4];                  // up to 4 help strings
} GameDesc;
```

`console_tick()` calls `tick()` then `draw()` each frame. When `tick()` returns `false`, the console captures the score, saves to NVS if a new best, plays the appropriate fanfare, and transitions to `CON_GAME_OVER`.

### UI Screens

**Select carousel:** Previous / current (2x, bracketed) / next game displayed vertically. Blinking `^`/`v` arrows. `BEST: nnnn` shown below.

**Pause overlay:** Frozen game frame drawn first, then a dark `PAL_BG_DARK` box overlaid with a `PAL_BLUE_MAIN` border. RESUME / HELP / QUIT selector inside. This was impossible on the previous monochrome OLED (no way to "dim" a region on a 1-bit display) — straightforward on color.

**Game over:** Ambient starfield behind. 2x `GAME OVER` in red. Score + best (or `** NEW BEST **` in gold). RETRY / MAIN MENU selector in a dark panel.

**Selector widget (`draw_selector`):** `PAL_BLUE_MAIN` highlight bar behind selected row, `PAL_WHITE` text on selected, `PAL_BG_DARK` on unselected. Blinking `>` cursor in `PAL_BG_DARK`.

---

## Boot Splash Animation

`splash.c` / `splash_font.c` run once at power-on before handing off to `console.c`. Five phases:

| Phase | Timing | Description |
|---|---|---|
| Black hold | 0–200ms | Display settle — prevents flicker on boot |
| Starburst | 200–600ms | Cross-lines expand from centre + filled square flash. All `tft_draw_line` / `tft_fill_rect` — no per-pixel loops (watchdog safety) |
| Typewriter | 600–1100ms | `"ESP32"` reveals one character at a time at 1x scale. Unrevealed chars shown as dim `PAL_OUTLINE` ghosts |
| Logo drop | 1100–1500ms | `"GAME"` (blue) and `"CONSOLE"` (gold) ease-out from above screen into final position |
| Scanline sweep | 1500–2000ms | Bright horizontal bar sweeps top-to-bottom — CRT "screen on" cue |
| Hold | 2000ms+ | Full logo with OUTLINE weight, ambient starfield, blinking `PRESS BTN TO START` |

### `splash_font.c` — Glyph Rendering Styles

```c
SPLASH_STYLE_BOLD     // clean single 2x draw (old stacked +1x/+1y removed — caused blur)
SPLASH_STYLE_OUTLINE  // two-pass: PAL_OUTLINE border at cardinal offsets, then full-color core
SPLASH_STYLE_GLOW     // 3×3 half-brightness halo, then clean core
SPLASH_STYLE_SLANTED  // explicit diagonal offset per character (never an accidental fallback)
```

Logo uses `OUTLINE` throughout — gives visual weight without destroying interior stroke geometry at 2x scale. `BOLD` stacking was the root cause of the "blurry, unreadable" text issue and has been fixed.

---

## Games

### Snake

Classic snake on a grid. The snake body is stored as a circular buffer of grid cells. Speed increases as the snake grows — `snake_get_step_ms()` returns a decreasing interval. Wall and self-collision end the game. Score = body length − starting length.

**Controls:** Joystick direction. Cannot reverse into self.

### Pong

Single-player: player paddle vs. a CPU paddle. Ball speed increases with each volley. CPU paddle has intentional imperfect tracking (misses occasionally) to keep the game winnable. Score increments on CPU miss; game ends when the player misses.

**Controls:** Joystick left/right moves player paddle.

### Breakout

Three rows of bricks, three lives. Ball reflects off paddle, walls, and bricks. Brick color indicates row (gold / blue / cyan). Clearing all bricks advances the level (bricks respawn, ball speeds up). Score = bricks cleared.

**Controls:** Joystick left/right moves paddle.

### Flappy Bird

Gravity-based side-scroller on the full 160×128 field (144px of vertical flight room vs. 56px on the old OLED — the single largest gameplay improvement from the display upgrade).

**Physics:** Q4 fixed-point (`FP=4`). Gravity `+3` per tick, flap `−46`, terminal velocity `+36`. 30ms tick interval.

**Background parallax:** Two procedural scrolling layers — distant hills (triangle-wave zigzag, zero storage) and clouds (3-rect sprites, drift at ⅓ pipe speed). Drawn back-to-front for correct occlusion.

**Ground:** Procedural grass strip (`PAL_GREEN_MAIN`) with blade detail, soil base (`PAL_BROWN`) with offset texture clumps (`PAL_BROWN_DARK`).

**Controls:** Action button (GPIO27) to flap.

### Space Invaders

Grid of invaders marching left/right and descending. Player cannon at bottom. Invaders fire back at random intervals. Shields degrade on hit. Score per invader destroyed; game ends if invaders reach the ground or player loses all lives.

**Controls:** Joystick left/right to move. Action button to shoot.

### Maze

Generated maze (recursive backtracker). Player navigates to the exit collecting coins along the way. Fog-of-war rendering — only visited cells and immediate neighbors are revealed. NVS high score tracks maximum coins collected in a single run.

**Controls:** Joystick to move. Pause button to exit to menu.

---

## Persistence — NVS High Scores

`nvs_scores.c` wraps ESP-IDF's `nvs_flash` API. Each game has one NVS key:

```
hi_snake / hi_pong / hi_breakout / hi_flappy / hi_invaders / hi_maze
```

On `console_init()`, all six scores are loaded with `nvs_scores_get()`. On game-over, if `score > s_hi[sel]`, `nvs_scores_set()` is called immediately. NVS survives power cycles and flash re-programming (stored in a dedicated NVS partition, not overwritten by `idf.py flash`).

---

## Visual Effects

`effects.c` provides two ambient effects used across menu screens and the game-over screen:

**Starfield (`effects_stars`):** A fixed array of star positions, each with a random brightness between `PAL_BLUE_BRIGHT` and `PAL_OUTLINE`. Stars are updated and drawn each frame. The pool is initialized once with `effects_stars_init(w, h)` and drawn with `effects_stars_draw()`.

**Heart sprites:** Small heart pixel-art sprites used in life displays (Breakout, Space Invaders). Drawn with `effects_draw_heart(x, y, color)`.

---

## Build & Flash

**Prerequisites:** ESP-IDF v6.0.1, CMake, Ninja.

```bash
# Set up IDF environment (Windows example)
. $IDF_PATH/export.ps1        # PowerShell
# or
. $IDF_PATH/export.sh         # bash

# Build
idf.py build

# Flash + monitor
idf.py -p COM3 flash monitor
```

**Target:** `esp32` (not esp32s2/s3 — uses ADC1 oneshot and dual-core pinning).

**Partition table:** Default with NVS partition. No custom partition table needed.

---

## Project Structure

```
main/
├── app_main.c          Entry point: splash → console loop
├── console.c/h         Top-level state machine + UI screens
├── palette.h           Shared RGB565 color palette
│
├── tft.c/h             ST7735S SPI display driver
├── font.c/h            Bitmap font renderer (1x and 2x)
├── effects.c/h         Starfield, heart sprites
├── sound.c/h           Non-blocking PWM audio (FreeRTOS queue)
├── joystick.c/h        ADC oneshot thumbstick reader
├── buttons.c/h         Debounced GPIO button reader
├── nvs_scores.c/h      NVS high score persistence
│
├── splash.c/h          Boot animation sequence
├── splash_font.c/h     Stylized 2x font renderers for splash
│
├── snake.c/h
├── pong.c/h
├── breakout.c/h
├── flappy.c/h
├── spaceinvaders.c/h
└── maze.c/h
```

---

## Design Decisions & Known Fixes

### OLED → TFT Migration

The project began on a 128×64 monochrome OLED. The TFT migration required:
- Full rewrite of display driver (SPI protocol, CASET/RASET window commands, RGB565 color)
- New shared font module (replaced per-file embedded font tables)
- All Y coordinates recalculated for 160px height — not a naive scale-up, a fresh layout pass
- Palette system introduced (monochrome had no color concept)
- Portrait→landscape orientation fix (MADCTL register + coordinate swap)

### ADC Watchdog Starvation

`draw_flash()` originally used nested `tft_draw_pixel` loops (up to 289 calls/frame). Fixed by replacing the pixel loop with a single `tft_fill_rect` call.

### RGB565 Palette Values

Initial palette constants had incorrect RGB565 packing — values like `PAL_GREEN_DARK 0x0340` produced near-black instead of forest green because channels were in the wrong bit positions. All earth and green values were recalculated from hex reference colors using the correct `(R5<<11)|(G6<<5)|B5` formula.

### Bold Font Blur

The splash `BOLD` renderer stacked three `font_draw_char_2x` calls at `(+0,+0)`, `(+1,+0)`, `(+0,+1)`. Fixed by removing the offset stacking; `OUTLINE` style (border pass then clean core) is used for logo text instead.
