#include "rl_battery.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
static const char *TAG = "rl_battery";
#define VBATT_FULL_MV  16600
#define VBATT_EMPTY_MV 12000
static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static bool s_cali_ok = false;
static uint32_t s_mv = 0;
static uint8_t s_pct = 100;
static void read_battery(void) {
    int raw=0, sum=0, vmv=0;
    for(int i=0;i<8;i++) { adc_oneshot_read(s_adc, BATT_ADC_CHANNEL, &raw); sum+=raw; vTaskDelay(pdMS_TO_TICKS(2)); }
    raw=sum/8;
    if(s_cali_ok) adc_cali_raw_to_voltage(s_cali, raw, &vmv);
    else vmv=(raw*3900)/4095;
    s_mv=((uint32_t)vmv*5626)/1000;
    if(s_mv>VBATT_FULL_MV) s_mv=VBATT_FULL_MV;
    if(s_mv<VBATT_EMPTY_MV) s_mv=VBATT_EMPTY_MV;
    s_pct=(uint8_t)(((s_mv-VBATT_EMPTY_MV)*100)/(VBATT_FULL_MV-VBATT_EMPTY_MV));
}
static void batt_task(void *arg) { while(1) { read_battery(); ESP_LOGI(TAG,"%umV=%u%%",s_mv,s_pct); vTaskDelay(pdMS_TO_TICKS(60000)); } }
void rl_battery_init(void) {
    adc_oneshot_unit_init_cfg_t uc={.unit_id=ADC_UNIT_1};
    adc_oneshot_new_unit(&uc,&s_adc);
    adc_oneshot_chan_cfg_t cc={.bitwidth=ADC_BITWIDTH_DEFAULT,.atten=ADC_ATTEN_DB_11};
    adc_oneshot_config_channel(s_adc,BATT_ADC_CHANNEL,&cc);
    adc_cali_line_fitting_config_t lc={.unit_id=ADC_UNIT_1,.atten=ADC_ATTEN_DB_11,.bitwidth=ADC_BITWIDTH_DEFAULT};
    if(adc_cali_create_scheme_line_fitting(&lc,&s_cali)==ESP_OK) s_cali_ok=true;
    read_battery();
    ESP_LOGI(TAG,"Battery init: %umV=%u%%",s_mv,s_pct);
    StaticTask_t *tcb = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    StackType_t *stack = heap_caps_malloc(4096 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (tcb && stack) xTaskCreateStaticPinnedToCore(batt_task, "rl_battery", 4096, NULL, 3, stack, tcb, 0);
}
void rl_battery_update(void) { read_battery(); }
uint8_t rl_battery_get_pct(void) { return s_pct; }
uint32_t rl_battery_get_mv(void) { return s_mv; }
bool rl_battery_is_low(void) { return s_pct<BATT_LOW_PCT; }
bool rl_battery_is_critical(void) { return s_pct<BATT_CRITICAL_PCT; }
