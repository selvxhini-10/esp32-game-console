#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void  pong_init(void);
void  pong_input(int dx, int dy, bool btn);
bool  pong_tick(uint32_t *score_out);
void  pong_draw(void);

#ifdef __cplusplus
}
#endif