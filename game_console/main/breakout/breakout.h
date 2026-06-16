#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void  breakout_init(void);
void  breakout_input(int dx, int dy, bool btn);
bool  breakout_tick(uint32_t *score_out);
void  breakout_draw(void);

#ifdef __cplusplus
}
#endif