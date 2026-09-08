#include "app_state.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static app_runtime_state_t s_state;

static void copy_text(char *destination, size_t size, const char *source)
{
    if (size == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    strlcpy(destination, source, size);
}

void app_state_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(&s_state, 0, sizeof(s_state));
    s_state.playback = PLAYBACK_STOPPED;
}

void app_state_set_lux(float lux)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.lux = lux;
    xSemaphoreGive(s_lock);
}

void app_state_set_wifi(bool connected, const char *ip_address)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.wifi_connected = connected;
    copy_text(s_state.ip_address, sizeof(s_state.ip_address), connected ? ip_address : "");
    xSemaphoreGive(s_lock);
}

void app_state_set_playback(playback_state_t state, const char *source)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.playback = state;
    if (source != NULL) {
        copy_text(s_state.playback_source, sizeof(s_state.playback_source), source);
    }
    if (state != PLAYBACK_ERROR) {
        s_state.last_error[0] = '\0';
    }
    xSemaphoreGive(s_lock);
}

void app_state_set_playback_loop(bool enabled)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.playback_loop = enabled;
    xSemaphoreGive(s_lock);
}

void app_state_set_error(const char *message)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_text(s_state.last_error, sizeof(s_state.last_error), message);
    s_state.playback = PLAYBACK_ERROR;
    xSemaphoreGive(s_lock);
}

void app_state_get(app_runtime_state_t *out)
{
    if (out == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
}

const char *app_state_playback_name(playback_state_t state)
{
    switch (state) {
        case PLAYBACK_BUFFERING: return "buffering";
        case PLAYBACK_PLAYING: return "playing";
        case PLAYBACK_ERROR: return "error";
        case PLAYBACK_STOPPED:
        default: return "stopped";
    }
}
