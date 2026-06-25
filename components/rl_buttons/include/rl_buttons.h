#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BTN_POWER_GPIO       32
#define BTN_PLAY_GPIO        33
#define BTN_VOL_UP_GPIO      27
#define BTN_VOL_DOWN_GPIO    14
#define BTN_MODE_GPIO        13

#define BTN_DEBOUNCE_MS         50
#define BTN_LONG_PRESS_MS       800
#define BTN_MULTI_PRESS_GAP_MS  400
#define BTN_COMBO_HOLD_MS       3000

typedef enum {
    BTN_EVT_NONE = 0,
    BTN_EVT_POWER_LONG,
    BTN_EVT_PLAY_SINGLE,
    BTN_EVT_PLAY_DOUBLE,
    BTN_EVT_PLAY_TRIPLE,
    BTN_EVT_VOL_UP,
    BTN_EVT_VOL_DOWN,
    BTN_EVT_VOL_UP_HELD,
    BTN_EVT_VOL_DOWN_HELD,
    BTN_EVT_MODE_SINGLE,
    BTN_EVT_MODE_LONG,
    BTN_EVT_COMBO_RESET,
    BTN_EVT_COMBO_PAIRING,
} btn_event_t;

typedef void (*btn_event_cb_t)(btn_event_t event);

void rl_buttons_init(btn_event_cb_t callback);
void rl_buttons_tick(void);
