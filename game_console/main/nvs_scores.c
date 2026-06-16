#include "nvs_scores.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG      = "nvs_scores";
static const char *NVS_NS   = "gamecons";   /* NVS namespace, max 15 chars */

void nvs_scores_init(void)
{
    esp_err_t err = nvs_flash_init();

    /* If NVS partition was truncated or has no free pages, erase and retry */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition problem (%s) — erasing", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    }
}

uint32_t nvs_scores_get(const char *game_key)
{
    nvs_handle_t h;
    uint32_t val = 0;

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return 0;

    /* Ignore ESP_ERR_NVS_NOT_FOUND — key simply doesn't exist yet */
    nvs_get_u32(h, game_key, &val);
    nvs_close(h);
    return val;
}

void nvs_scores_set(const char *game_key, uint32_t score)
{
    nvs_handle_t h;

    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed for key '%s'", game_key);
        return;
    }

    nvs_set_u32(h, game_key, score);
    nvs_commit(h);
    nvs_close(h);
}