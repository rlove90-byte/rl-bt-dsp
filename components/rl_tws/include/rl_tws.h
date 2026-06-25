#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define TWS_MASTER_BEACON_INTERVAL_MS 500
#define TWS_SLAVE_SEARCH_MS           3000
#define TWS_SLAVE_TIMEOUT_MS          2000
#define TWS_AUDIO_CHANNEL_SIZE        1024
typedef enum { TWS_ROLE_SEARCHING=0, TWS_ROLE_MASTER, TWS_ROLE_SLAVE } tws_role_t;
typedef void (*tws_audio_cb_t)(const uint8_t *data, size_t len, uint32_t play_time_ms);
typedef void (*tws_slave_cb_t)(bool connected);
void rl_tws_init(tws_audio_cb_t audio_cb, tws_slave_cb_t slave_cb);
tws_role_t rl_tws_get_role(void);
bool rl_tws_slave_connected(void);
void rl_tws_send_audio(const uint8_t *stereo_pcm, size_t len, uint32_t play_time_ms);
void rl_tws_reset(void);
