#pragma once
#include <stdint.h>
#include <stdbool.h>
#define RL_LED_GPIO  18
#define RL_LED_COUNT 16
typedef enum {
    LED_MODE_OFF=0, LED_MODE_BOOT, LED_MODE_BATTERY,
    LED_MODE_PAIRING, LED_MODE_CONNECTING,
    LED_MODE_INDOOR, LED_MODE_OUTDOOR, LED_MODE_ROOM_CORRECTION,
    LED_MODE_LOW_BATTERY, LED_MODE_PLAYING,
} led_mode_t;
void rl_leds_init(void);
void rl_leds_set_mode(led_mode_t mode);
void rl_leds_show_battery(uint8_t battery_pct);
void rl_leds_set_brightness(uint8_t brightness);
