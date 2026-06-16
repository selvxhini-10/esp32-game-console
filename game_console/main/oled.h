#pragma once

#include <stdint.h>
#include <stdbool.h>

#define OLED_WIDTH  128
#define OLED_HEIGHT 64

#ifdef __cplusplus
extern "C" {
#endif

void oled_init(void);

void oled_clear(void);

void oled_update(void);

void oled_draw_pixel(int x, int y);

void oled_draw_rect(int x, int y, int w, int h);

#ifdef __cplusplus
}
#endif