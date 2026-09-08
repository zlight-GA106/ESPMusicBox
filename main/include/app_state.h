#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PLAYBACK_STOPPED = 0,
    PLAYBACK_BUFFERING,
    PLAYBACK_PLAYING,
    PLAYBACK_ERROR,
} playback_state_t;

typedef struct {
    float lux;
    bool wifi_connected;
    char ip_address[16];
    playback_state_t playback;
    bool playback_loop;
    char playback_source[160];
    char last_error[96];
} app_runtime_state_t;

void app_state_init(void);
void app_state_set_lux(float lux);
void app_state_set_wifi(bool connected, const char *ip_address);
void app_state_set_playback(playback_state_t state, const char *source);
void app_state_set_playback_loop(bool enabled);
void app_state_set_error(const char *message);
void app_state_get(app_runtime_state_t *out);
const char *app_state_playback_name(playback_state_t state);
