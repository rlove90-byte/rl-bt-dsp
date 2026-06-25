#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_adc/adc_oneshot.h"
#define BATT_ADC_CHANNEL    ADC_CHANNEL_7
#define BATT_LOW_PCT        15
#define BATT_CRITICAL_PCT   5
void rl_battery_init(void);
uint8_t rl_battery_get_pct(void);
uint32_t rl_battery_get_mv(void);
bool rl_battery_is_low(void);
bool rl_battery_is_critical(void);
