#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"

#define LITTLEFS_BASE_PATH "/littlefs"
#define MUSIC_DIRECTORY "/littlefs/music"
#define CONFIG_MIRROR_PATH "/littlefs/config/config.json"

esp_err_t filesystem_init(void);
void storage_task(void *arg);
size_t filesystem_total_bytes(void);
size_t filesystem_used_bytes(void);
cJSON *filesystem_list_music(void);
esp_err_t filesystem_delete_music(const char *name);
esp_err_t filesystem_upload_begin(const char *name, size_t expected_size);
esp_err_t filesystem_upload_write(const uint8_t *data, size_t length);
esp_err_t filesystem_upload_end(bool commit);
bool filesystem_safe_music_name(const char *name);
void filesystem_music_path(const char *name, char *out, size_t out_size);
