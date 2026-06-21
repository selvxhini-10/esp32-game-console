#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SOUND — passive buzzer on GPIO13, driven via LEDC PWM.
 *
 * Non-blocking by design: sound_play() pushes a tone request onto a
 * FreeRTOS queue and returns immediately. A dedicated audio task plays
 * tones in the background so game loops never stall waiting on sound.
 *
 * ── A NOTE ON VOLUME ──────────────────────────────────────────────────
 * A passive buzzer's loudness in software is governed almost entirely by
 * PWM duty cycle, and 50% duty is already the acoustic maximum a square
 * wave can produce — you cannot go louder than that without a hardware
 * change (transistor driver stage, bigger buzzer, etc). What software CAN
 * do:
 *   - sound_set_volume() scales duty cycle 0-100% for relative loudness
 *     control (e.g. quieter UI blips vs louder game-over stingers)
 *   - sound_punch() briefly drives the buzzer at its resonant frequency
 *     (~2-4kHz on most small piezo buzzers) which is physically louder
 *     than musical notes even at the same duty cycle — useful for
 *     impact/hit/explosion effects where "loud" matters more than pitch
 * ──────────────────────────────────────────────────────────────────────
 */

/* Standard note frequencies (Hz) — enough for full melodies across 2 octaves */
#define NOTE_REST   0     /* silence — use to create gaps/rests in a tune */

#define NOTE_C3   131
#define NOTE_D3   147
#define NOTE_E3   165
#define NOTE_F3   175
#define NOTE_G3   196
#define NOTE_A3   220
#define NOTE_B3   247

#define NOTE_C4   262
#define NOTE_CS4  277
#define NOTE_D4   294
#define NOTE_DS4  311
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_FS4  370
#define NOTE_G4   392
#define NOTE_GS4  415
#define NOTE_A4   440
#define NOTE_AS4  466
#define NOTE_B4   494

#define NOTE_C5   523
#define NOTE_CS5  554
#define NOTE_D5   587
#define NOTE_DS5  622
#define NOTE_E5   659
#define NOTE_F5   698
#define NOTE_FS5  740
#define NOTE_G5   784
#define NOTE_GS5  831
#define NOTE_A5   880
#define NOTE_B5   988

#define NOTE_C6   1047

/*
 * A single note in a melody.
 * freq_hz = 0 (NOTE_REST) means silence for that duration — used to
 * create rhythm/spacing between notes, or rests within a tune.
 */
typedef struct {
    uint32_t freq_hz;
    uint32_t duration_ms;
} Note;

void sound_init(void);

/* Non-blocking — queues a single tone and returns immediately */
void sound_play(uint32_t freq_hz, uint32_t duration_ms);

/*
 * Non-blocking — queues an entire melody (array of Notes) as one unit.
 * The audio task plays them back-to-back without gaps other than what
 * you encode via NOTE_REST entries. Game code can fire-and-forget a
 * full tune the same way it fires a single sound_play() call.
 *
 * count = number of notes in the array (use sizeof(arr)/sizeof(arr[0]))
 */
void sound_play_melody(const Note *notes, int count);

/* Stop any currently playing tone/melody immediately and clear the queue */
void sound_stop(void);

/*
 * Relative volume as a percentage of max duty cycle (0-100).
 * 100 = current default (50% PWM duty, the acoustic maximum).
 * Lower values reduce duty cycle proportionally for quieter effects.
 * Affects all sound_play()/sound_play_melody() calls after this is set.
 */
void sound_set_volume(uint8_t percent);

/*
 * Briefly drives the buzzer at its physical resonant frequency for a
 * sharp, loud "punch" — good for explosions, hits, or game-over stingers
 * where impact matters more than musical pitch. Non-blocking, like
 * sound_play(). resonant_hz is buzzer-dependent; 2700-3200Hz covers most
 * small piezo buzzers — experiment with your specific part for the
 * loudest result.
 */
void sound_punch(uint32_t resonant_hz, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif