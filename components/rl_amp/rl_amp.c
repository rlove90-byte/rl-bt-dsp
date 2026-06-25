#include "rl_amp.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "rl_amp";
void rl_amp_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask=(1ULL<<AMP_MUTE_GPIO), .mode=GPIO_MODE_OUTPUT,
        .pull_up_en=GPIO_PULLUP_DISABLE, .pull_down_en=GPIO_PULLDOWN_DISABLE,
        .intr_type=GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    rl_amp_mute();
    ESP_LOGI(TAG, "Amp init, muted");
}
void rl_amp_mute(void) { gpio_set_level(AMP_MUTE_GPIO, 1); }
void rl_amp_unmute(void) { vTaskDelay(pdMS_TO_TICKS(100)); gpio_set_level(AMP_MUTE_GPIO, 0); ESP_LOGI(TAG, "Amp unmuted"); }
