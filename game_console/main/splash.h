#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Boot splash screen — plays once on power-on.
 * Call splash_start() once, then call splash_tick() every loop
 * until it returns false, then hand off to the console.
 */
void splash_start(void);
bool splash_tick(void);   /* returns false when animation is complete */

#ifdef __cplusplus
}
#endif