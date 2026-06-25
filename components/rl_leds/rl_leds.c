
#include "rl_leds.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

static const char *TAG = "rl_leds";

typedef struct { uint8_t g,r,b; } rgb_t;

static rmt_channel_handle_t s_chan = NULL;
static rmt_encoder_handle_t s_enc = NULL;
static rgb_t s_pixels[RL_LED_COUNT];
static led_mode_t s_mode = LED_MODE_OFF;
static led_mode_t s_prev = LED_MODE_INDOOR;
static uint8_t s_brightness = 180;
static bool s_batt_showing = false;
static uint32_t s_batt_start = 0;
static uint8_t s_batt_pct = 100;
static uint32_t s_tick = 0;

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *bytes_enc;
    rmt_encoder_t *copy_enc;
    int state;
    rmt_symbol_word_t reset_code;
} ws2812_enc_t;

static size_t ws2812_encode(rmt_encoder_t *enc, rmt_channel_handle_t ch, const void *data, size_t sz, rmt_encode_state_t *ret) {
    ws2812_enc_t *e = __containerof(enc, ws2812_enc_t, base);
    rmt_encode_state_t ss=RMT_ENCODING_RESET, st=RMT_ENCODING_RESET;
    size_t n=0;
    if(e->state==0) {
        n+=e->bytes_enc->encode(e->bytes_enc,ch,data,sz,&ss);
        if(ss&RMT_ENCODING_COMPLETE) e->state=1;
        if(ss&RMT_ENCODING_MEM_FULL){st|=RMT_ENCODING_MEM_FULL;goto out;}
    }
    if(e->state==1) {
        n+=e->copy_enc->encode(e->copy_enc,ch,&e->reset_code,sizeof(e->reset_code),&ss);
        if(ss&RMT_ENCODING_COMPLETE){e->state=RMT_ENCODING_RESET;st|=RMT_ENCODING_COMPLETE;}
        if(ss&RMT_ENCODING_MEM_FULL) st|=RMT_ENCODING_MEM_FULL;
    }
out:
    *ret=st; return n;
}
static esp_err_t ws2812_del(rmt_encoder_t *enc) {
    ws2812_enc_t *e=__containerof(enc,ws2812_enc_t,base);
    rmt_del_encoder(e->bytes_enc); rmt_del_encoder(e->copy_enc); free(e); return ESP_OK;
}
static esp_err_t ws2812_reset(rmt_encoder_t *enc) {
    ws2812_enc_t *e=__containerof(enc,ws2812_enc_t,base);
    rmt_encoder_reset(e->bytes_enc); rmt_encoder_reset(e->copy_enc); e->state=RMT_ENCODING_RESET; return ESP_OK;
}
static esp_err_t new_ws2812_enc(rmt_encoder_handle_t *ret) {
    ws2812_enc_t *e=calloc(1,sizeof(ws2812_enc_t));
    e->base.encode=ws2812_encode; e->base.del=ws2812_del; e->base.reset=ws2812_reset;
    rmt_bytes_encoder_config_t bc={
        .bit0={.level0=1,.duration0=4,.level1=0,.duration1=8},
        .bit1={.level0=1,.duration0=8,.level1=0,.duration1=4},
        .flags.msb_first=1};
    rmt_new_bytes_encoder(&bc,&e->bytes_enc);
    rmt_copy_encoder_config_t cc={};
    rmt_new_copy_encoder(&cc,&e->copy_enc);
    e->reset_code=(rmt_symbol_word_t){.level0=0,.duration0=1400,.level1=0,.duration1=1400};
    *ret=&e->base; return ESP_OK;
}

static void write_pixels(void) {
    rgb_t buf[RL_LED_COUNT];
    for(int i=0;i<RL_LED_COUNT;i++){
        buf[i].g=(s_pixels[i].g*s_brightness)/255;
        buf[i].r=(s_pixels[i].r*s_brightness)/255;
        buf[i].b=(s_pixels[i].b*s_brightness)/255;
    }
    rmt_transmit_config_t tx={.loop_count=0};
    rmt_transmit(s_chan,s_enc,buf,sizeof(buf),&tx);
    rmt_tx_wait_all_done(s_chan,portMAX_DELAY);
}

static void fill(uint8_t g,uint8_t r,uint8_t b) {
    for(int i=0;i<RL_LED_COUNT;i++){s_pixels[i].g=g;s_pixels[i].r=r;s_pixels[i].b=b;}
    write_pixels();
}

static void anim_pulse(uint8_t g,uint8_t r,uint8_t b,uint32_t tick) {
    float phase=(tick%100)/100.0f;
    float br=(sinf(phase*2.0f*3.14159f)+1.0f)/2.0f;
    uint8_t bv=(uint8_t)(br*200);
    fill((g*bv)/255,(r*bv)/255,(b*bv)/255);
}

static void anim_battery(uint8_t pct) {
    int lit=(RL_LED_COUNT*pct)/100;
    if(lit==0&&pct>0) lit=1;
    for(int i=0;i<RL_LED_COUNT;i++){
        if(i<lit){
            if(pct>50){s_pixels[i].g=200;s_pixels[i].r=0;s_pixels[i].b=0;}
            else if(pct>20){s_pixels[i].g=165;s_pixels[i].r=80;s_pixels[i].b=0;}
            else{s_pixels[i].g=0;s_pixels[i].r=180;s_pixels[i].b=0;}
        } else {s_pixels[i].g=0;s_pixels[i].r=0;s_pixels[i].b=0;}
    }
    write_pixels();
}

static void anim_boot(uint32_t tick) {
    int pos=tick%RL_LED_COUNT;
    for(int i=0;i<RL_LED_COUNT;i++){
        s_pixels[i].g=(i==pos)?10:0;
        s_pixels[i].r=(i==pos)?0:0;
        s_pixels[i].b=(i==pos)?180:0;
    }
    write_pixels();
}

static void led_task(void *arg) {
    while(1){
        uint32_t now=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
        if(s_batt_showing&&(now-s_batt_start)>=3000){s_batt_showing=false;s_mode=s_prev;}
        switch(s_mode){
        case LED_MODE_OFF:           fill(0,0,0); break;
        case LED_MODE_BOOT:          anim_boot(s_tick); break;
        case LED_MODE_BATTERY:       anim_battery(s_batt_pct); break;
        case LED_MODE_PAIRING:
        case LED_MODE_CONNECTING:    anim_pulse(10,0,180,s_tick); break;
        case LED_MODE_INDOOR:        fill(0,0,200); break;
        case LED_MODE_OUTDOOR:       fill(0,200,0); break;
        case LED_MODE_ROOM_CORRECTION: fill(165,80,0); break;
        case LED_MODE_LOW_BATTERY:   anim_pulse(0,180,0,s_tick); break;
        default: break;
        }
        s_tick++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void rl_leds_init(void) {
    rmt_tx_channel_config_t cc={.gpio_num=RL_LED_GPIO,.clk_src=RMT_CLK_SRC_DEFAULT,.resolution_hz=10000000,.mem_block_symbols=64,.trans_queue_depth=4};
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cc,&s_chan));
    ESP_ERROR_CHECK(new_ws2812_enc(&s_enc));
    ESP_ERROR_CHECK(rmt_enable(s_chan));
    fill(0,0,0);
    s_mode=LED_MODE_BOOT;
    xTaskCreate(led_task,"rl_leds",3072,NULL,4,NULL);
    ESP_LOGI(TAG,"LED init: %d LEDs GPIO%d",RL_LED_COUNT,RL_LED_GPIO);
}

void rl_leds_set_mode(led_mode_t mode) { s_prev=s_mode; s_mode=mode; }

void rl_leds_show_battery(uint8_t pct) {
    s_batt_pct=pct; s_prev=s_mode; s_mode=LED_MODE_BATTERY;
    s_batt_showing=true;
    s_batt_start=(uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS);
}

void rl_leds_set_brightness(uint8_t b) { s_brightness=b; }
