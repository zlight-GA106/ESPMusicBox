#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"

#define CONFIG_WIFI_SSID_MAX 32
#define CONFIG_WIFI_PASSWORD_MAX 64
#define CONFIG_RADIO_URL_MAX 256
#define CONFIG_MUSIC_NAME_MAX 96

typedef enum {
    APP_MODE_BIRTHDAY = 0,
    APP_MODE_RADIO,
} app_mode_t;

typedef enum {
    TRIGGER_ABOVE = 0,
    TRIGGER_BELOW,
} trigger_direction_t;

typedef struct {
    app_mode_t mode;
    char wifi_ssid[CONFIG_WIFI_SSID_MAX + 1];
    char wifi_password[CONFIG_WIFI_PASSWORD_MAX + 1];
    uint8_t volume;
    trigger_direction_t trigger_direction;
    float trigger_lux;
    float dead_zone_lux;
    char radio_url[CONFIG_RADIO_URL_MAX + 1];
    uint16_t birthday_count;
    char birthday_file[CONFIG_MUSIC_NAME_MAX + 1];
    bool play_on_boot;
    bool play_boot_loop;
} app_config_t;

esp_err_t nvs_config_init(void);
void nvs_config_get(app_config_t *out);
uint32_t nvs_config_revision(void);
esp_err_t nvs_config_update_json(const cJSON *json, char *error, size_t error_size);
cJSON *nvs_config_to_json(void);
esp_err_t nvs_config_write_mirror(void);
const char *nvs_config_mode_name(app_mode_t mode);
