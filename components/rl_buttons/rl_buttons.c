#include "rl_buttons.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "rl_buttons";

typedef enum { BTN_ID_POWER=0, BTN_ID_PLAY, BTN_ID_VOL_UP, BTN_ID_VOL_DOWN, BTN_ID_MODE, BTN_COUNT } btn_id_t;
typedef enum { BS_IDLE=0, BS_PRESSED, BS_HELD, BS_WAIT_MULTI } btn_state_t;

typedef struct {
    int gpio; btn_state_t state;
    uint32_t press_time_ms, release_time_ms;
    int tap_count; bool held_fired;
} btn_ctx_t;

static btn_ctx_t s_btns[BTN_COUNT];
static btn_event_cb_t s_callback = NULL;
static const int s_gpio_map[BTN_COUNT] = { BTN_POWER_GPIO, BTN_PLAY_GPIO, BTN_VOL_UP_GPIO, BTN_VOL_DOWN_GPIO, BTN_MODE_GPIO };

static uint32_t now_ms(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }
static void fire(btn_event_t e) { if (s_callback) s_callback(e); }
static bool btn_pressed(btn_id_t id) { return gpio_get_level(s_gpio_map[id]) == 0; }
static bool both_pressed(btn_id_t a, btn_id_t b) { return btn_pressed(a) && btn_pressed(b); }

static void process_btn(btn_id_t id) {
    btn_ctx_t *b = &s_btns[id];
    uint32_t t = now_ms();
    bool pressed = btn_pressed(id);
    switch (b->state) {
    case BS_IDLE:
        if (pressed) { b->press_time_ms=t; b->state=BS_PRESSED; b->held_fired=false; }
        break;
    case BS_PRESSED:
        if (!pressed) { b->release_time_ms=t; b->tap_count++; b->state=BS_WAIT_MULTI; }
        else {
            uint32_t held = t - b->press_time_ms;
            if (id==BTN_ID_VOL_UP && both_pressed(BTN_ID_VOL_UP,BTN_ID_VOL_DOWN)) {
                if (held>=BTN_COMBO_HOLD_MS && !b->held_fired) { b->held_fired=true; b->state=BS_HELD; fire(BTN_EVT_COMBO_RESET); } break;
            }
            if (id==BTN_ID_POWER && both_pressed(BTN_ID_POWER,BTN_ID_PLAY)) {
                if (held>=BTN_COMBO_HOLD_MS && !b->held_fired) { b->held_fired=true; b->state=BS_HELD; fire(BTN_EVT_COMBO_PAIRING); } break;
            }
            if (held>=BTN_LONG_PRESS_MS && !b->held_fired) {
                b->held_fired=true; b->state=BS_HELD;
                switch(id) {
                case BTN_ID_POWER: fire(BTN_EVT_POWER_LONG); break;
                case BTN_ID_MODE: fire(BTN_EVT_MODE_LONG); break;
                case BTN_ID_VOL_UP: fire(BTN_EVT_VOL_UP_HELD); break;
                case BTN_ID_VOL_DOWN: fire(BTN_EVT_VOL_DOWN_HELD); break;
                default: break;
                }
            }
        }
        break;
    case BS_HELD:
        if (!pressed) { b->state=BS_IDLE; b->tap_count=0; }
        break;
    case BS_WAIT_MULTI:
        if (pressed) { b->press_time_ms=t; b->state=BS_PRESSED; }
        else if (t-b->release_time_ms >= BTN_MULTI_PRESS_GAP_MS) {
            switch(id) {
            case BTN_ID_PLAY:
                if (b->tap_count==1) fire(BTN_EVT_PLAY_SINGLE);
                else if (b->tap_count==2) fire(BTN_EVT_PLAY_DOUBLE);
                else if (b->tap_count>=3) fire(BTN_EVT_PLAY_TRIPLE);
                break;
            case BTN_ID_VOL_UP: fire(BTN_EVT_VOL_UP); break;
            case BTN_ID_VOL_DOWN: fire(BTN_EVT_VOL_DOWN); break;
            case BTN_ID_MODE: fire(BTN_EVT_MODE_SINGLE); break;
            default: break;
            }
            b->tap_count=0; b->state=BS_IDLE;
        }
        break;
    default: b->state=BS_IDLE; break;
    }
}

static void btn_task(void *arg) {
    while(1) { for(int i=0;i<BTN_COUNT;i++) process_btn((btn_id_t)i); vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS)); }
}

void rl_buttons_init(btn_event_cb_t callback) {
    s_callback = callback;
    memset(s_btns, 0, sizeof(s_btns));
    gpio_config_t cfg = { .mode=GPIO_MODE_INPUT, .pull_up_en=GPIO_PULLUP_ENABLE, .pull_down_en=GPIO_PULLDOWN_DISABLE, .intr_type=GPIO_INTR_DISABLE };
    for(int i=0;i<BTN_COUNT;i++) {
        s_btns[i].gpio=s_gpio_map[i]; s_btns[i].state=BS_IDLE;
        cfg.pin_bit_mask=(1ULL<<s_gpio_map[i]); gpio_config(&cfg);
    }
    xTaskCreate(btn_task,"rl_buttons",2048,NULL,5,NULL);
}
void rl_buttons_tick(void) {}
