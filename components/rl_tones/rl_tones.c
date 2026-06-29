#include "rl_tones.h"
#include "audio_output.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

static const char *TAG = "rl_tones";

#define SAMPLE_RATE     44100
#define TONE_AMPLITUDE  8000  /* quiet enough not to be jarring */

static QueueHandle_t s_tone_queue = NULL;
static volatile bool s_tones_enabled = true;

/* Generate a sine wave tone and write to I2S */
static void play_beep(float freq_hz, int duration_ms, float fade_ms) {
    int num_samples = (SAMPLE_RATE * duration_ms) / 1000;
    int fade_samples = (SAMPLE_RATE * (int)fade_ms) / 1000;
    int16_t buf[64 * 2]; /* stereo, 64 samples at a time */
    int written_total = 0;

    while (written_total < num_samples) {
        int chunk = num_samples - written_total;
        if (chunk > 64) chunk = 64;
        for (int i = 0; i < chunk; i++) {
            int s = written_total + i;
            float t = (float)s / SAMPLE_RATE;
            float envelope = 1.0f;
            /* fade in */
            if (s < fade_samples) envelope = (float)s / fade_samples;
            /* fade out */
            if (s > num_samples - fade_samples)
                envelope = (float)(num_samples - s) / fade_samples;
            int16_t sample = (int16_t)(sinf(2.0f * M_PI * freq_hz * t) * TONE_AMPLITUDE * envelope);
            buf[i * 2]     = sample; /* L */
            buf[i * 2 + 1] = sample; /* R */
        }
        esp_err_t err = audio_output_write(buf, chunk * 4, pdMS_TO_TICKS(10));
        if (err != ESP_OK) { return; } /* channel disabled, abort */
        written_total += chunk;
        taskYIELD();
    }
}

static void play_silence(int duration_ms) {
    int num_samples = (SAMPLE_RATE * duration_ms) / 1000;
    int16_t buf[64 * 2];
    memset(buf, 0, sizeof(buf));
    int written = 0;
    while (written < num_samples) {
        int chunk = num_samples - written;
        if (chunk > 64) chunk = 64;
        esp_err_t err = audio_output_write(buf, chunk * 4, pdMS_TO_TICKS(10));
        if (err != ESP_OK) { return; }
        written += chunk;
        taskYIELD();
    }
}

static void do_play_tone(rl_tone_t tone) {
    switch (tone) {
    case TONE_POWER_ON:
        play_beep(523, 80, 10);  /* C5 */
        play_silence(30);
        play_beep(659, 80, 10);  /* E5 */
        play_silence(30);
        play_beep(784, 120, 20); /* G5 */
        break;
    case TONE_POWER_OFF:
        play_beep(784, 80, 10);  /* G5 */
        play_silence(30);
        play_beep(659, 80, 10);  /* E5 */
        play_silence(30);
        play_beep(523, 120, 20); /* C5 */
        break;
    case TONE_BT_CONNECTED:
        play_beep(880, 80, 10);  /* A5 */
        play_silence(40);
        play_beep(1047, 100, 15); /* C6 */
        break;
    case TONE_BT_DISCONNECTED:
        play_beep(1047, 80, 10); /* C6 */
        play_silence(40);
        play_beep(880, 100, 15); /* A5 */
        break;
    case TONE_MODE_CHANGE:
        play_beep(880, 80, 10);  /* A5 */
        break;
    case TONE_VOLUME_LIMIT:
        play_beep(1200, 50, 5);
        break;
    case TONE_WIFI_SETUP:
        /* ascending sweep feel */
        play_beep(440, 60, 10);
        play_silence(20);
        play_beep(660, 60, 10);
        play_silence(20);
        play_beep(880, 60, 10);
        play_silence(20);
        play_beep(1100, 100, 15);
        break;
    case TONE_WIFI_CONNECTED:
        play_beep(523, 60, 10);
        play_silence(20);
        play_beep(659, 60, 10);
        play_silence(20);
        play_beep(784, 60, 10);
        play_silence(20);
        play_beep(1047, 150, 20);
        break;
    case TONE_WIFI_FAILED:
        play_beep(400, 150, 15);
        play_silence(40);
        play_beep(300, 200, 20);
        break;
    case TONE_ROOM_CORRECTION_START:
        play_beep(440, 40, 5);
        play_silence(15);
        play_beep(550, 40, 5);
        play_silence(15);
        play_beep(660, 40, 5);
        play_silence(15);
        play_beep(770, 40, 5);
        break;
    case TONE_ROOM_CORRECTION_DONE:
        play_beep(784, 80, 10);
        play_silence(30);
        play_beep(880, 80, 10);
        play_silence(30);
        play_beep(1047, 150, 20);
        break;
    default:
        break;
    }
}

static void tone_task(void *arg) {
    rl_tone_t tone;
    while (1) {
        if (xQueueReceive(s_tone_queue, &tone, portMAX_DELAY)) {
            do_play_tone(tone);
        }
    }
}

void rl_tones_init(void) {
    s_tone_queue = xQueueCreate(4, sizeof(rl_tone_t));
    xTaskCreate(tone_task, "rl_tones", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Tones init");
}

void rl_tones_play(rl_tone_t tone) {
    if (s_tone_queue && s_tones_enabled) {
        xQueueSend(s_tone_queue, &tone, 0);
    }
}
void rl_tones_set_enabled(bool enabled) { s_tones_enabled = enabled; }
