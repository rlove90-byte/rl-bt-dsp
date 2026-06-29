
#include "rl_buttons.h"
#include "rl_leds.h"
#include "rl_battery.h"
#include "rl_tws.h"
#include "rl_amp.h"
#include "esp_log.h"
#include "rtsp_events.h"
#include "rl_app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

static const char *TAG = "rl_main";

typedef enum { DSP_MODE_INDOOR=0, DSP_MODE_OUTDOOR, DSP_MODE_ROOM_CORRECTION } dsp_mode_t;
static dsp_mode_t s_dsp = DSP_MODE_INDOOR;
static bool s_on = true;

static void apply_dsp(dsp_mode_t mode) {
    s_dsp=mode;
    switch(mode){
    case DSP_MODE_INDOOR:          ESP_LOGI(TAG,"DSP: Indoor");          rl_leds_set_mode(LED_MODE_INDOOR); break;
    case DSP_MODE_OUTDOOR:         ESP_LOGI(TAG,"DSP: Outdoor");         rl_leds_set_mode(LED_MODE_OUTDOOR); break;
    case DSP_MODE_ROOM_CORRECTION: ESP_LOGI(TAG,"DSP: Room correction"); rl_leds_set_mode(LED_MODE_ROOM_CORRECTION); break;
    }
}

static void on_button(btn_event_t e) {
    switch(e){
    case BTN_EVT_POWER_LONG:
        if(s_on){ s_on=false; rl_amp_mute(); rl_leds_set_mode(LED_MODE_OFF); ESP_LOGI(TAG,"Power off"); }
        else { s_on=true; rl_leds_show_battery(rl_battery_get_pct()); rl_amp_unmute(); apply_dsp(s_dsp); ESP_LOGI(TAG,"Power on"); }
        break;
    case BTN_EVT_PLAY_SINGLE:  ESP_LOGI(TAG,"Play/Pause"); break;
    case BTN_EVT_PLAY_DOUBLE:  ESP_LOGI(TAG,"Next track"); break;
    case BTN_EVT_PLAY_TRIPLE:  ESP_LOGI(TAG,"Prev track"); break;
    case BTN_EVT_VOL_UP:
    case BTN_EVT_VOL_UP_HELD:  ESP_LOGI(TAG,"Vol+"); break;
    case BTN_EVT_VOL_DOWN:
    case BTN_EVT_VOL_DOWN_HELD:ESP_LOGI(TAG,"Vol-"); break;
    case BTN_EVT_MODE_SINGLE:  apply_dsp(s_dsp==DSP_MODE_INDOOR ? DSP_MODE_OUTDOOR : DSP_MODE_INDOOR); break;
    case BTN_EVT_MODE_LONG:    apply_dsp(DSP_MODE_ROOM_CORRECTION); break;
    case BTN_EVT_COMBO_RESET:
        ESP_LOGW(TAG,"Reset!"); rl_leds_set_mode(LED_MODE_OFF);
        vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); break;
    case BTN_EVT_COMBO_PAIRING:
        ESP_LOGI(TAG,"Pairing mode"); rl_leds_set_mode(LED_MODE_PAIRING); break;
    default: break;
    }
}

static void on_tws_audio(const uint8_t *data, size_t len, uint32_t play_time_ms) {
    // Slave receives right channel - feed to I2S (TODO: hook into audio pipeline)
    (void)data; (void)len; (void)play_time_ms;
}

static void on_tws_slave(bool connected) {
    if(connected) ESP_LOGI(TAG,"TWS: slave connected - stereo");
    else          ESP_LOGI(TAG,"TWS: slave gone - mono");
}

static void batt_task(void *arg) {
    while(1){
        vTaskDelay(pdMS_TO_TICKS(60000));
        if(rl_battery_is_critical() && rl_battery_get_mv() > 12100){
            ESP_LOGW(TAG,"Battery critical %u%%",rl_battery_get_pct());
            rl_leds_set_mode(LED_MODE_LOW_BATTERY);
            rl_amp_mute();
        } else if(rl_battery_is_low() && rl_battery_get_mv() > 12100){
            ESP_LOGW(TAG,"Battery low %u%%",rl_battery_get_pct());
            rl_leds_set_mode(LED_MODE_LOW_BATTERY);
        }
    }
}

static void on_rtsp_event(rtsp_event_t event, const rtsp_event_data_t *data, void *user_data) {
    (void)data; (void)user_data;
    switch(event) {
    case RTSP_EVENT_PLAYING:  rl_app_on_bt_playing(true);  break;
    case RTSP_EVENT_PAUSED:   rl_app_on_bt_playing(false); break;
    case RTSP_EVENT_DISCONNECTED: rl_app_on_bt_connected(false); break;
    default: break;
    }
}

void rl_app_on_bt_connected(bool connected) {
    if (connected) {
        rl_leds_set_bt_state(LED_BT_CONNECTED);
    } else {
        rl_leds_set_bt_state(LED_BT_DISCONNECTED);
    }
}

void rl_app_on_bt_playing(bool playing) {
    rl_leds_set_bt_state(playing ? LED_BT_PLAYING : LED_BT_CONNECTED);
}

void rl_app_init(void) {
    ESP_LOGI(TAG,"RL BT DSP starting");
    rl_amp_init();
    rl_leds_init();
    rl_leds_set_mode(LED_MODE_BOOT);
    rl_battery_init();
    rl_buttons_init(on_button);
    rl_tws_init(on_tws_audio, on_tws_slave);
    vTaskDelay(pdMS_TO_TICKS(2000));
    rl_leds_show_battery(75); // TODO: replace with rl_battery_get_pct() when battery connected
    vTaskDelay(pdMS_TO_TICKS(1000));
    apply_dsp(DSP_MODE_INDOOR);
    xTaskCreate(batt_task,"batt_mon",4096,NULL,2,NULL);
    rl_amp_unmute();
    rtsp_events_register(on_rtsp_event, NULL);
    ESP_LOGI(TAG,"RL BT DSP init complete");
}
