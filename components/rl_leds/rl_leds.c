#include "rl_leds.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "rl_leds";

typedef struct { uint8_t g, r, b; } rgb_t;

static rmt_channel_handle_t s_chan = NULL;
static rmt_encoder_handle_t s_enc  = NULL;
static rgb_t     s_pixels[RL_LED_COUNT];
static led_mode_t     s_mode      = LED_MODE_OFF;
static led_bt_state_t s_bt_state  = LED_BT_DISCONNECTED;
static bool      s_batt_showing   = false;
static uint32_t  s_batt_start     = 0;
static uint8_t   s_batt_pct       = 100;
static uint32_t  s_tick           = 0;

/* Brightness levels */
#define BR_PLAYING      30    /* 12% */
#define BR_CONNECTED    12    /*  5% */
#define BR_DISCONNECTED  5    /*  2% */
#define BR_BOOT         20

/* Base colours (full brightness, GRB order: G, R, B) */
#define COL_INDOOR      0, 150, 255  /* purple: G=0,   R=150, B=255 */
#define COL_OUTDOOR   255,   0, 150  /* teal:   G=255, R=0,   B=150 */
#define COL_ROOM    100, 255,   0    /* orange: G=100, R=255, B=0   */

/* ── RMT / WS2812 encoder ─────────────────────────────────────────────────── */
typedef struct {
    rmt_encoder_t  base;
    rmt_encoder_t *bytes_enc;
    rmt_encoder_t *copy_enc;
    int            state;
    rmt_symbol_word_t reset_code;
} ws2812_enc_t;

static size_t ws2812_encode(rmt_encoder_t *enc, rmt_channel_handle_t ch,
                             const void *data, size_t sz, rmt_encode_state_t *ret) {
    ws2812_enc_t *e = __containerof(enc, ws2812_enc_t, base);
    rmt_encode_state_t ss = RMT_ENCODING_RESET, st = RMT_ENCODING_RESET;
    size_t n = 0;
    if (e->state == 0) {
        n += e->bytes_enc->encode(e->bytes_enc, ch, data, sz, &ss);
        if (ss & RMT_ENCODING_COMPLETE) e->state = 1;
        if (ss & RMT_ENCODING_MEM_FULL) { st |= RMT_ENCODING_MEM_FULL; goto out; }
    }
    if (e->state == 1) {
        n += e->copy_enc->encode(e->copy_enc, ch, &e->reset_code, sizeof(e->reset_code), &ss);
        if (ss & RMT_ENCODING_COMPLETE) { e->state = RMT_ENCODING_RESET; st |= RMT_ENCODING_COMPLETE; }
        if (ss & RMT_ENCODING_MEM_FULL) st |= RMT_ENCODING_MEM_FULL;
    }
out:
    *ret = st; return n;
}
static esp_err_t ws2812_del(rmt_encoder_t *enc) {
    ws2812_enc_t *e = __containerof(enc, ws2812_enc_t, base);
    rmt_del_encoder(e->bytes_enc); rmt_del_encoder(e->copy_enc); free(e); return ESP_OK;
}
static esp_err_t ws2812_reset(rmt_encoder_t *enc) {
    ws2812_enc_t *e = __containerof(enc, ws2812_enc_t, base);
    rmt_encoder_reset(e->bytes_enc); rmt_encoder_reset(e->copy_enc);
    e->state = RMT_ENCODING_RESET; return ESP_OK;
}
static esp_err_t new_ws2812_enc(rmt_encoder_handle_t *ret) {
    ws2812_enc_t *e = calloc(1, sizeof(ws2812_enc_t));
    e->base.encode = ws2812_encode; e->base.del = ws2812_del; e->base.reset = ws2812_reset;
    rmt_bytes_encoder_config_t bc = {
        .bit0 = {.level0=1,.duration0=4,.level1=0,.duration1=8},
        .bit1 = {.level0=1,.duration0=8,.level1=0,.duration1=4},
        .flags.msb_first = 1};
    rmt_new_bytes_encoder(&bc, &e->bytes_enc);
    rmt_copy_encoder_config_t cc = {};
    rmt_new_copy_encoder(&cc, &e->copy_enc);
    e->reset_code = (rmt_symbol_word_t){.level0=0,.duration0=8000,.level1=0,.duration1=8000};
    *ret = &e->base; return ESP_OK;
}

/* ── Pixel helpers ────────────────────────────────────────────────────────── */
static void write_pixels(uint8_t brightness) {
    rgb_t buf[RL_LED_COUNT];
    for (int i = 0; i < RL_LED_COUNT; i++) {
        buf[i].g = (s_pixels[i].g * brightness) / 255;
        buf[i].r = (s_pixels[i].r * brightness) / 255;
        buf[i].b = (s_pixels[i].b * brightness) / 255;
    }
    rmt_transmit_config_t tx = {.loop_count = 0};
    rmt_transmit(s_chan, s_enc, buf, sizeof(buf), &tx);
    rmt_tx_wait_all_done(s_chan, portMAX_DELAY);
}

static void fill(uint8_t g, uint8_t r, uint8_t b, uint8_t brightness) {
    for (int i = 0; i < RL_LED_COUNT; i++) {
        s_pixels[i].g = g; s_pixels[i].r = r; s_pixels[i].b = b;
    }
    write_pixels(brightness);
}

static uint8_t bt_brightness(void) {
    switch (s_bt_state) {
        case LED_BT_PLAYING:      return BR_PLAYING;
        case LED_BT_CONNECTED:    return BR_CONNECTED;
        default:                  return BR_DISCONNECTED;
    }
}

static void anim_battery(uint8_t pct) {
    int lit = (RL_LED_COUNT * pct + 99) / 100; /* round up */
    if (lit == 0 && pct > 0) lit = 1;
    if (lit > RL_LED_COUNT) lit = RL_LED_COUNT;
    for (int i = 0; i < RL_LED_COUNT; i++) {
        if (i < lit) { s_pixels[i].g = 200; s_pixels[i].r = 0; s_pixels[i].b = 0; }
        else         { s_pixels[i].g = 0;   s_pixels[i].r = 0; s_pixels[i].b = 0; }
    }
    write_pixels(BR_PLAYING);
}

static void anim_boot(uint32_t tick) {
    int pos = tick % RL_LED_COUNT;
    for (int i = 0; i < RL_LED_COUNT; i++) {
        s_pixels[i].g = 0;
        s_pixels[i].r = 0;
        s_pixels[i].b = (i == pos) ? 255 : 0;
    }
    write_pixels(BR_BOOT);
}

/* ── LED task ─────────────────────────────────────────────────────────────── */
static void led_task(void *arg) {
    while (1) {
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (s_batt_showing && (now - s_batt_start) >= 3000) {
            s_batt_showing = false;
        }

        uint8_t br = bt_brightness();

        if (s_batt_showing) {
            anim_battery(s_batt_pct);
        } else {
            switch (s_mode) {
            case LED_MODE_OFF:             fill(0,0,0,0); break;
            case LED_MODE_BOOT:            anim_boot(s_tick); break;
            case LED_MODE_PAIRING:         anim_boot(s_tick); break;
            case LED_MODE_LOW_BATTERY:     fill(0, 180, 0, BR_PLAYING); break;
            case LED_MODE_INDOOR:          fill(COL_INDOOR,  br); break;
            case LED_MODE_OUTDOOR:         fill(COL_OUTDOOR, br); break;
            case LED_MODE_ROOM_CORRECTION: fill(COL_ROOM,    br); break;
            default: break;
            }
        }
        s_tick++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */
void rl_leds_init(void) {
    rmt_tx_channel_config_t cc = {
        .gpio_num = RL_LED_GPIO, .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000, .mem_block_symbols = 64, .trans_queue_depth = 4};
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cc, &s_chan));
    ESP_ERROR_CHECK(new_ws2812_enc(&s_enc));
    ESP_ERROR_CHECK(rmt_enable(s_chan));
    fill(0, 0, 0, 0);
    s_mode = LED_MODE_BOOT;
    xTaskCreate(led_task, "rl_leds", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "LED init: %d LEDs GPIO%d", RL_LED_COUNT, RL_LED_GPIO);
}

void rl_leds_set_mode(led_mode_t mode)         { s_mode = mode; }
void rl_leds_set_bt_state(led_bt_state_t state) { s_bt_state = state; }
void rl_leds_set_brightness(uint8_t b)          { (void)b; }

void rl_leds_show_battery(uint8_t pct) {
    s_batt_pct = pct;
    s_batt_showing = true;
    s_batt_start = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
