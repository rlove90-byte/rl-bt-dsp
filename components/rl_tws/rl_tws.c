
#include "rl_tws.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "rl_tws";
#define PKT_BEACON    0x01
#define PKT_SLAVE_ACK 0x02
#define PKT_AUDIO     0x03
#define PKT_GONE      0x04

typedef struct __attribute__((packed)) { uint8_t type; uint8_t mac[6]; uint32_t ts; } tws_beacon_t;
typedef struct __attribute__((packed)) { uint8_t type; uint8_t mac[6]; } tws_ack_t;
typedef struct __attribute__((packed)) { uint8_t type; uint32_t play_time_ms; uint16_t len; uint8_t data[TWS_AUDIO_CHANNEL_SIZE]; } tws_audio_pkt_t;

static tws_role_t s_role = TWS_ROLE_SEARCHING;
static uint8_t s_my_mac[6], s_peer_mac[6];
static bool s_peer_reg = false, s_slave_conn = false;
static uint32_t s_last_beacon=0, s_last_seen=0, s_search_start=0;
static tws_audio_cb_t s_audio_cb = NULL;
static tws_slave_cb_t s_slave_cb = NULL;
static const uint8_t BC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static uint32_t now_ms(void) { return (uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS); }

static void reg_peer(const uint8_t *mac) {
    if(s_peer_reg) esp_now_del_peer(s_peer_mac);
    memcpy(s_peer_mac,mac,6);
    esp_now_peer_info_t p={.channel=0,.ifidx=WIFI_IF_STA,.encrypt=false};
    memcpy(p.peer_addr,mac,6);
    esp_now_add_peer(&p);
    s_peer_reg=true;
}

static void become_master(void) {
    s_role=TWS_ROLE_MASTER; s_slave_conn=false;
    ESP_LOGI(TAG,"Role: MASTER");
    if(!esp_now_is_peer_exist(BC)) {
        esp_now_peer_info_t p={.channel=0,.ifidx=WIFI_IF_STA,.encrypt=false};
        memcpy(p.peer_addr,BC,6); esp_now_add_peer(&p);
    }
}

static void become_slave(const uint8_t *master_mac) {
    s_role=TWS_ROLE_SLAVE;
    reg_peer(master_mac);
    s_last_seen=now_ms();
    ESP_LOGI(TAG,"Role: SLAVE");
    tws_ack_t ack={.type=PKT_SLAVE_ACK};
    memcpy(ack.mac,s_my_mac,6);
    esp_now_send(master_mac,(uint8_t*)&ack,sizeof(ack));
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if(len<1) return;
    switch(data[0]) {
    case PKT_BEACON:
        if(s_role==TWS_ROLE_SEARCHING) { become_slave(((tws_beacon_t*)data)->mac); }
        break;
    case PKT_SLAVE_ACK:
        if(s_role==TWS_ROLE_MASTER && !s_slave_conn) {
            reg_peer(((tws_ack_t*)data)->mac);
            s_slave_conn=true; s_last_seen=now_ms();
            if(s_slave_cb) s_slave_cb(true);
            ESP_LOGI(TAG,"Slave connected");
        }
        break;
    case PKT_AUDIO:
        if(s_role==TWS_ROLE_SLAVE) {
            tws_audio_pkt_t *p=(tws_audio_pkt_t*)data;
            s_last_seen=now_ms();
            if(s_audio_cb) s_audio_cb(p->data,p->len,p->play_time_ms);
        }
        break;
    case PKT_GONE:
        if(s_role==TWS_ROLE_SLAVE) {
            ESP_LOGI(TAG,"Master gone, promoting in 5s");
            vTaskDelay(pdMS_TO_TICKS(5000));
            become_master();
        }
        break;
    }
}

static void on_sent(const wifi_tx_info_t *tx_info, esp_now_send_status_t s) { (void)tx_info; (void)s; }

static void tws_task(void *arg) {
    s_search_start=now_ms();
    while(1) {
        uint32_t t=now_ms();
        switch(s_role) {
        case TWS_ROLE_SEARCHING:
            if(t-s_search_start>=TWS_SLAVE_SEARCH_MS) { ESP_LOGI(TAG,"No master found, becoming master"); become_master(); }
            break;
        case TWS_ROLE_MASTER:
            if(t-s_last_beacon>=TWS_MASTER_BEACON_INTERVAL_MS) {
                s_last_beacon=t;
                tws_beacon_t b={.type=PKT_BEACON,.ts=t};
                memcpy(b.mac,s_my_mac,6);
                esp_now_send(BC,(uint8_t*)&b,sizeof(b));
            }
            if(s_slave_conn && (t-s_last_seen)>TWS_SLAVE_TIMEOUT_MS) {
                ESP_LOGW(TAG,"Slave timeout"); s_slave_conn=false;
                if(s_slave_cb) s_slave_cb(false);
            }
            break;
        case TWS_ROLE_SLAVE:
            if((t-s_last_seen)>TWS_SLAVE_TIMEOUT_MS) {
                ESP_LOGW(TAG,"Master timeout, promoting in 5s");
                vTaskDelay(pdMS_TO_TICKS(5000)); become_master();
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void rl_tws_init(tws_audio_cb_t audio_cb, tws_slave_cb_t slave_cb) {
    s_audio_cb=audio_cb; s_slave_cb=slave_cb;
    esp_read_mac(s_my_mac,ESP_MAC_WIFI_STA);
    esp_now_init();
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_sent);
    xTaskCreate(tws_task,"rl_tws",4096,NULL,6,NULL);
    ESP_LOGI(TAG,"TWS init, searching %dms",TWS_SLAVE_SEARCH_MS);
}

tws_role_t rl_tws_get_role(void) { return s_role; }
bool rl_tws_slave_connected(void) { return s_slave_conn; }

void rl_tws_send_audio(const uint8_t *pcm, size_t len, uint32_t play_time_ms) {
    if(s_role!=TWS_ROLE_MASTER||!s_slave_conn) return;
    tws_audio_pkt_t pkt={.type=PKT_AUDIO,.play_time_ms=play_time_ms};
    size_t samples=len/4;
    size_t out=samples*2; if(out>TWS_AUDIO_CHANNEL_SIZE) out=TWS_AUDIO_CHANNEL_SIZE;
    for(size_t i=0;i<out/2;i++) { pkt.data[i*2]=pcm[i*4+2]; pkt.data[i*2+1]=pcm[i*4+3]; }
    pkt.len=(uint16_t)out;
    esp_now_send(s_peer_mac,(uint8_t*)&pkt,sizeof(pkt)-TWS_AUDIO_CHANNEL_SIZE+out);
}

void rl_tws_reset(void) {
    s_role=TWS_ROLE_SEARCHING; s_slave_conn=false; s_peer_reg=false;
    s_search_start=now_ms();
}
