#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t audio_service_init(void);
esp_err_t audio_service_play_birthday(uint16_t repeat_count);
/* repeat_count == 0 means infinite single-track looping. */
esp_err_t audio_service_play_file(const char *music_name, uint16_t repeat_count);
esp_err_t audio_service_start_radio(const char *url);
void audio_service_stop(void);
bool audio_service_is_active(void);
void audio_service_set_volume(uint8_t volume);
void audio_task(void *arg);
