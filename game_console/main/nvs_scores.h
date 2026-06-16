#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thin wrapper around ESP-IDF NVS (Non-Volatile Storage) for per-game high score persistence.
 * Scores survive power cycles and reflashing (NVS partition is separate).
 *
 * Usage:
 *   nvs_scores_init();              // call once at boot
 *   uint32_t hi = nvs_scores_get("snake");
 *   nvs_scores_set("snake", 120);
 */

void     nvs_scores_init(void);
uint32_t nvs_scores_get(const char *game_key);
void     nvs_scores_set(const char *game_key, uint32_t score);

#ifdef __cplusplus
}
#endif