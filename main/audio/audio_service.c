#include "audio_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "app_state.h"
#include "i2s_player.h"
#include "littlefs.h"
#include "mp3_decoder.h"
#include "nvs_config.h"
#include "wav_decoder.h"

#define AUDIO_STOP_BIT BIT0
#define WAV_BUFFER_SAMPLES 4096

typedef enum {
    AUDIO_COMMAND_FILE,
    AUDIO_COMMAND_RADIO,
} audio_command_type_t;

typedef struct {
    audio_command_type_t type;
    char source[CONFIG_RADIO_URL_MAX + 1];
    uint16_t repeat_count;
} audio_command_t;

typedef struct {
    esp_http_client_handle_t client;
} http_input_t;

static const char *TAG = "audio";
static QueueHandle_t s_queue;
static EventGroupHandle_t s_events;
static SemaphoreHandle_t s_guard;
static bool s_active;
static bool s_pending;

static bool stop_requested(void *context)
{
    (void)context;
    return (xEventGroupGetBits(s_events) & AUDIO_STOP_BIT) != 0;
}

static int file_read(void *context, uint8_t *buffer, size_t length)
{
    FILE *file = (FILE *)context;
    size_t bytes = fread(buffer, 1, length, file);
    if (bytes == 0 && ferror(file)) return -1;
    return (int)bytes;
}

static int http_read(void *context, uint8_t *buffer, size_t length)
{
    http_input_t *input = (http_input_t *)context;
    return esp_http_client_read(input->client, (char *)buffer, (int)length);
}

static esp_err_t play_wav(const char *path)
{
    wav_decoder_t wav;
    if (!wav_decoder_open(&wav, path)) return ESP_ERR_INVALID_RESPONSE;
    int16_t *buffer = heap_caps_malloc(WAV_BUFFER_SAMPLES * sizeof(int16_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) buffer = malloc(WAV_BUFFER_SAMPLES * sizeof(int16_t));
    if (buffer == NULL) {
        wav_decoder_close(&wav);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = i2s_player_configure(wav.sample_rate);
    if (err == ESP_OK) app_state_set_playback(PLAYBACK_PLAYING, path);
    while (err == ESP_OK && !stop_requested(NULL)) {
        size_t samples = wav_decoder_read(&wav, buffer, WAV_BUFFER_SAMPLES);
        if (samples == 0) break;
        err = i2s_player_write(buffer, samples, (uint8_t)wav.channels);
    }
    free(buffer);
    wav_decoder_close(&wav);
    return err;
}

static esp_err_t play_local_once(const char *path)
{
    const char *extension = strrchr(path, '.');
    if (extension != NULL && strcasecmp(extension, ".wav") == 0) return play_wav(path);
    if (extension != NULL && strcasecmp(extension, ".mp3") == 0) {
        FILE *file = fopen(path, "rb");
        if (file == NULL) return ESP_ERR_NOT_FOUND;
        app_state_set_playback(PLAYBACK_BUFFERING, path);
        esp_err_t err = mp3_decode_stream(file_read, file, stop_requested, NULL);
        fclose(file);
        return err;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t play_radio(const char *url)
{
    esp_err_t last_error = ESP_FAIL;
    while (!stop_requested(NULL)) {
        app_state_set_playback(PLAYBACK_BUFFERING, url);
        esp_http_client_config_t config = {
            .url = url,
            .timeout_ms = 10000,
            .buffer_size = 4096,
            .buffer_size_tx = 1024,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .keep_alive_enable = true,
            .disable_auto_redirect = false,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client != NULL) {
            last_error = esp_http_client_open(client, 0);
            if (last_error == ESP_OK) {
                int64_t content_length = esp_http_client_fetch_headers(client);
                int status = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG, "radio HTTP status=%d length=%lld", status, content_length);
                if (status >= 200 && status < 300) {
                    http_input_t input = {.client = client};
                    last_error = mp3_decode_stream(http_read, &input, stop_requested, NULL);
                } else {
                    last_error = ESP_ERR_INVALID_RESPONSE;
                }
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        } else {
            last_error = ESP_ERR_NO_MEM;
        }
        if (stop_requested(NULL)) break;
        app_state_set_error("radio disconnected; reconnecting");
        for (int i = 0; i < 20 && !stop_requested(NULL); ++i) vTaskDelay(pdMS_TO_TICKS(100));
    }
    return stop_requested(NULL) ? ESP_OK : last_error;
}

esp_err_t audio_service_init(void)
{
    s_queue = xQueueCreate(3, sizeof(audio_command_t));
    s_events = xEventGroupCreate();
    s_guard = xSemaphoreCreateMutex();
    if (s_queue == NULL || s_events == NULL || s_guard == NULL) return ESP_ERR_NO_MEM;
    return i2s_player_init();
}

static esp_err_t queue_command(const audio_command_t *command)
{
    if (command == NULL) return ESP_ERR_INVALID_ARG;
    xEventGroupSetBits(s_events, AUDIO_STOP_BIT);
    xQueueReset(s_queue);
    xSemaphoreTake(s_guard, portMAX_DELAY);
    s_pending = true;
    xSemaphoreGive(s_guard);
    if (xQueueSend(s_queue, command, pdMS_TO_TICKS(100)) != pdTRUE) {
        xSemaphoreTake(s_guard, portMAX_DELAY);
        s_pending = false;
        xSemaphoreGive(s_guard);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t audio_service_play_file(const char *music_name, uint16_t repeat_count)
{
    if (!filesystem_safe_music_name(music_name)) return ESP_ERR_INVALID_ARG;
    audio_command_t command = {.type = AUDIO_COMMAND_FILE, .repeat_count = repeat_count};
    filesystem_music_path(music_name, command.source, sizeof(command.source));
    return queue_command(&command);
}

esp_err_t audio_service_play_birthday(uint16_t repeat_count)
{
    app_config_t config;
    nvs_config_get(&config);
    return audio_service_play_file(config.birthday_file, repeat_count);
}

esp_err_t audio_service_start_radio(const char *url)
{
    if (url == NULL || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) ||
        strlen(url) >= sizeof(((audio_command_t *)0)->source)) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_command_t command = {.type = AUDIO_COMMAND_RADIO, .repeat_count = 1};
    strlcpy(command.source, url, sizeof(command.source));
    return queue_command(&command);
}

void audio_service_stop(void)
{
    if (s_events == NULL) return;
    xEventGroupSetBits(s_events, AUDIO_STOP_BIT);
    if (s_queue != NULL) xQueueReset(s_queue);
    if (s_guard != NULL) {
        xSemaphoreTake(s_guard, portMAX_DELAY);
        s_pending = false;
        xSemaphoreGive(s_guard);
    }
}

bool audio_service_is_active(void)
{
    if (s_guard == NULL) return false;
    xSemaphoreTake(s_guard, portMAX_DELAY);
    bool active = s_active || s_pending;
    xSemaphoreGive(s_guard);
    return active;
}

void audio_service_set_volume(uint8_t volume)
{
    i2s_player_set_volume(volume);
}

void audio_task(void *arg)
{
    (void)arg;
    audio_command_t command;
    for (;;) {
        if (xQueueReceive(s_queue, &command, portMAX_DELAY) != pdTRUE) continue;
        xSemaphoreTake(s_guard, portMAX_DELAY);
        s_pending = false;
        s_active = true;
        xSemaphoreGive(s_guard);
        xEventGroupClearBits(s_events, AUDIO_STOP_BIT);

        esp_err_t err = ESP_OK;
        app_state_set_playback_loop(command.type == AUDIO_COMMAND_FILE && command.repeat_count == 0);
        if (command.type == AUDIO_COMMAND_RADIO) {
            err = play_radio(command.source);
        } else {
            uint16_t played = 0;
            while (!stop_requested(NULL) &&
                   (command.repeat_count == 0 || played < command.repeat_count)) {
                err = play_local_once(command.source);
                if (err != ESP_OK) break;
                ++played;
            }
        }
        (void)i2s_player_silence();
        bool stopped = stop_requested(NULL);
        app_state_set_playback_loop(false);
        if (err == ESP_OK || stopped) {
            app_state_set_playback(PLAYBACK_STOPPED, "");
        } else {
            char message[96];
            snprintf(message, sizeof(message), "audio failed: %s", esp_err_to_name(err));
            app_state_set_error(message);
            ESP_LOGE(TAG, "%s", message);
        }
        xSemaphoreTake(s_guard, portMAX_DELAY);
        s_active = false;
        xSemaphoreGive(s_guard);
    }
}
