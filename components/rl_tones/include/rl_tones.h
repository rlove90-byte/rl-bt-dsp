#pragma once
#include <stdbool.h>

typedef enum {
    TONE_BT_CONNECTED,
    TONE_BT_DISCONNECTED,
    TONE_POWER_ON,
    TONE_POWER_OFF,
    TONE_MODE_CHANGE,
    TONE_VOLUME_LIMIT,
    TONE_WIFI_SETUP,
    TONE_WIFI_CONNECTED,
    TONE_WIFI_FAILED,
    TONE_ROOM_CORRECTION_START,
    TONE_ROOM_CORRECTION_DONE,
} rl_tone_t;

void rl_tones_init(void);
void rl_tones_play(rl_tone_t tone);
void rl_tones_set_enabled(bool enabled);
