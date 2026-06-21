/*
 * SOUND — passive buzzer driver for GPIO13 using the ESP32 LEDC peripheral.
 *
 * Passive buzzers have no internal oscillator — the pitch you hear is
 * exactly the frequency of the square wave driving them. LEDC (LED Control)
 * is normally used for dimming LEDs, but it's just a PWM generator, so it
 * doubles perfectly as a tone generator: set the PWM frequency to the note
 * frequency, and the buzzer reproduces that pitch.
 *
 * MELODY SUPPORT:
 * The original version only supported single sound_play(freq, ms) calls,
 * so anything beyond a 2-3 note jingle meant the caller had to issue
 * several sound_play() calls back-to-back. Those calls land in the same
 * queue and get interleaved with anything else queued in between (e.g. if
 * two game events fire sounds close together, their notes could get mixed
 * up). sound_play_melody() fixes this by queuing an entire Note array as
 * ONE atomic unit — the audio task plays all notes in the array back-to-
 * back with no other sound able to interrupt mid-melody, then moves on to
 * whatever's next in the queue.
 *
 * VOLUME:
 * Software volume on a passive buzzer is fundamentally a duty-cycle scalar.
 * 50% duty is the acoustic maximum for a square wave, so "100% volume"
 * here means the original 50% PWM duty — there's no headroom above that
 * without a hardware driver stage. Lower volume settings scale the duty
 * cycle down proportionally, which does measurably reduce loudness (useful
 * for quieter UI feedback vs louder game-over stingers).
 *
 * Non-blocking architecture (unchanged from before):
 *   game code calls sound_play()/sound_play_melody() → pushes event(s)
 *   onto a queue → returns immediately. A dedicated audio task on CPU1
 *   consumes the queue and drives the buzzer, blocking ONLY inside that
 *   task — never inside the caller's game loop.
 */

#include "sound.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "sound";

/* ─── hardware config ────────────────────────────────────────────────────── */
#define BUZZER_GPIO       GPIO_NUM_13
#define LEDC_TIMER_SEL    LEDC_TIMER_1
#define LEDC_CHANNEL_SEL  LEDC_CHANNEL_1
/*
 * Duty resolution lowered from 10-bit to 8-bit.
 *
 * Why: LEDC's clock divider has limited precision, and the achievable
 * frequency range shrinks as duty resolution increases (max_freq is
 * roughly source_clock / 2^duty_resolution). At 10-bit resolution, some
 * frequencies in the upper end of the musical note range used here
 * (e.g. NOTE_C6 = 1047 Hz) land on a divider value that rounds to zero,
 * which the driver rejects with "div_param=0" and silently fails to set
 * the frequency — meaning that note plays at whatever frequency was
 * already configured, not the one requested.
 *
 * 10-bit resolution (1024 duty steps) makes sense for smooth analog
 * brightness fades on an LED, but a square-wave tone generator only ever
 * needs ON (50% duty) or OFF (0% duty) — fine-grained duty control buys
 * nothing here. Dropping to 8-bit (256 duty steps) raises the maximum
 * achievable frequency from ~78kHz to ~312kHz, comfortably covering the
 * full musical range (131-1047 Hz) used by this sound module with no
 * audible difference in tone quality.
 */
#define LEDC_DUTY_RES     LEDC_TIMER_8_BIT    /* 0-255 duty range           */
#define LEDC_DUTY_MAX     128                 /* 50% of 255 — acoustic max  */

/* ─── melody queue item ──────────────────────────────────────────────────── *
 * Each queue entry is either:
 *   - a single note  (count == 1, notes[0] used)
 *   - a melody        (count > 1, up to MAX_MELODY_NOTES notes copied in)
 *
 * Melodies are copied BY VALUE into the queue item (not by pointer) so the
 * caller's array — which is often a local `const Note tune[] = {...}` on
 * the stack — doesn't need to stay alive after sound_play_melody() returns.
 * This also means melodies must fit in MAX_MELODY_NOTES; anything longer
 * gets truncated (logged as a warning) rather than overflowing memory.
 * ────────────────────────────────────────────────────────────────────────── */
#define MAX_MELODY_NOTES   16

typedef struct {
    Note notes[MAX_MELODY_NOTES];
    int  count;
} SoundEvent;

static QueueHandle_t s_sound_queue       = NULL;
static TaskHandle_t  s_sound_task_handle = NULL;

/* Current volume scalar: duty = LEDC_DUTY_MAX * s_volume_pct / 100 */
static uint8_t s_volume_pct = 100;

/* ─── low-level tone control ─────────────────────────────────────────────── */

static void buzzer_set_tone(uint32_t freq_hz)
{
    if (freq_hz == 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SEL, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SEL);
        return;
    }

    uint32_t duty = (uint32_t)LEDC_DUTY_MAX * s_volume_pct / 100;

    /*
     * Check the return value — if a future note frequency falls outside
     * what the current duty resolution can express, ledc_set_freq() fails
     * and logs its own ESP_LOGE("ledc", ...) error. We additionally log
     * here with our own tag so it's unambiguous which frequency/call
     * triggered it, rather than relying on parsing the driver's message.
     */
    esp_err_t err = ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_SEL, freq_hz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ledc_set_freq(%u Hz) failed: %s — tone skipped",
                 (unsigned)freq_hz, esp_err_to_name(err));
        return;   /* don't proceed to set duty on an unconfigured frequency */
    }

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_SEL);
}

/* ─── audio task — the ONLY place that blocks on sound timing ───────────── */

static void audio_task(void *arg)
{
    (void)arg;
    SoundEvent ev;

    while (1) {
        if (xQueueReceive(s_sound_queue, &ev, portMAX_DELAY) == pdTRUE) {
            /* Play every note in this event back-to-back, uninterrupted */
            for (int i = 0; i < ev.count; i++) {
                buzzer_set_tone(ev.notes[i].freq_hz);
                vTaskDelay(pdMS_TO_TICKS(ev.notes[i].duration_ms));
            }
            buzzer_set_tone(0);   /* silence once the whole event finishes */
        }
    }
}

/* ─── public API ─────────────────────────────────────────────────────────── */

void sound_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = LEDC_TIMER_SEL,
        .freq_hz         = NOTE_C4,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return;
    }

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_SEL,
        .timer_sel  = LEDC_TIMER_SEL,
        .duty       = 0,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ch_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return;
    }

    /*
     * Queue depth increased from 8 to 4 — each item can now contain up to
     * 16 notes, so 4 queued melodies is already a substantial backlog.
     * This keeps queue memory bounded: 4 * sizeof(SoundEvent) where
     * SoundEvent is 16 * 8 bytes + 4 bytes ≈ 132 bytes → ~528 bytes total,
     * a small, predictable allocation.
     */
    s_sound_queue = xQueueCreate(4, sizeof(SoundEvent));
    if (!s_sound_queue) {
        ESP_LOGE(TAG, "Failed to create sound queue");
        return;
    }

    /* Pinned to CPU1 — see prior fix notes: keeps audio_task off CPU0 so it
     * can never compete with app_main/IDLE0 and risk starving the watchdog. */
    xTaskCreatePinnedToCore(audio_task, "audio_task", 2048, NULL, 3,
                             &s_sound_task_handle, 1 /* CPU1 */);

    ESP_LOGI(TAG, "Sound initialised on GPIO%d", BUZZER_GPIO);
}

void sound_play(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!s_sound_queue) return;

    SoundEvent ev = {
        .notes = { { freq_hz, duration_ms } },
        .count = 1,
    };

    xQueueSend(s_sound_queue, &ev, 0);   /* drop if queue full — never block caller */
}

void sound_play_melody(const Note *notes, int count)
{
    if (!s_sound_queue || !notes || count <= 0) return;

    if (count > MAX_MELODY_NOTES) {
        ESP_LOGW(TAG, "Melody truncated: %d notes requested, max is %d",
                 count, MAX_MELODY_NOTES);
        count = MAX_MELODY_NOTES;
    }

    SoundEvent ev;
    memcpy(ev.notes, notes, sizeof(Note) * count);
    ev.count = count;

    xQueueSend(s_sound_queue, &ev, 0);
}

void sound_stop(void)
{
    buzzer_set_tone(0);
    if (s_sound_queue) xQueueReset(s_sound_queue);
}

void sound_set_volume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_volume_pct = percent;
}

void sound_punch(uint32_t resonant_hz, uint32_t duration_ms)
{
    /* A punch is just a one-note "melody" at the buzzer's loudest frequency */
    sound_play(resonant_hz, duration_ms);
}