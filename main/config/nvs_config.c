#include "nvs_config.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "littlefs.h"

static const char *TAG = "config";
static const char *NAMESPACE = "musicbox";
static SemaphoreHandle_t s_lock;
static app_config_t s_config;
static uint32_t s_revision;

static void set_defaults(app_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->mode = APP_MODE_BIRTHDAY;
    config->volume = 80;
    config->lux_threshold = 200.0f;
    config->birthday_count = 1;
    strlcpy(config->birthday_file, "birthday.wav", sizeof(config->birthday_file));
}

static esp_err_t load_string(nvs_handle_t handle, const char *key, char *value, size_t capacity)
{
    size_t required = capacity;
    esp_err_t err = nvs_get_str(handle, key, value, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        value[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

static esp_err_t persist_locked(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, "mode", (uint8_t)s_config.mode);
    if (err == ESP_OK) err = nvs_set_str(handle, "ssid", s_config.wifi_ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "password", s_config.wifi_password);
    if (err == ESP_OK) err = nvs_set_u8(handle, "volume", s_config.volume);
    if (err == ESP_OK) err = nvs_set_blob(handle, "lux", &s_config.lux_threshold, sizeof(float));
    if (err == ESP_OK) err = nvs_set_str(handle, "radio_url", s_config.radio_url);
    if (err == ESP_OK) err = nvs_set_u16(handle, "bday_count", s_config.birthday_count);
    if (err == ESP_OK) err = nvs_set_str(handle, "bday_file", s_config.birthday_file);
    if (err == ESP_OK) err = nvs_set_u8(handle, "play_boot", s_config.play_on_boot ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, "play_boot_loop", s_config.play_boot_loop ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_config_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    set_defaults(&s_config);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    uint8_t mode = (uint8_t)s_config.mode;
    (void)nvs_get_u8(handle, "mode", &mode);
    if (mode <= APP_MODE_RADIO) s_config.mode = (app_mode_t)mode;
    (void)load_string(handle, "ssid", s_config.wifi_ssid, sizeof(s_config.wifi_ssid));
    (void)load_string(handle, "password", s_config.wifi_password, sizeof(s_config.wifi_password));
    (void)nvs_get_u8(handle, "volume", &s_config.volume);
    size_t float_size = sizeof(float);
    (void)nvs_get_blob(handle, "lux", &s_config.lux_threshold, &float_size);
    (void)load_string(handle, "radio_url", s_config.radio_url, sizeof(s_config.radio_url));
    (void)nvs_get_u16(handle, "bday_count", &s_config.birthday_count);
    (void)load_string(handle, "bday_file", s_config.birthday_file, sizeof(s_config.birthday_file));
    uint8_t play_on_boot = 0;
    (void)nvs_get_u8(handle, "play_boot", &play_on_boot);
    s_config.play_on_boot = play_on_boot != 0;
    uint8_t play_boot_loop = 0;
    (void)nvs_get_u8(handle, "play_boot_loop", &play_boot_loop);
    s_config.play_boot_loop = play_boot_loop != 0;
    nvs_close(handle);

    if (s_config.volume > 100) s_config.volume = 100;
    if (s_config.lux_threshold < 1.0f) s_config.lux_threshold = 1.0f;
    if (s_config.birthday_count == 0) s_config.birthday_count = 1;
    if (!filesystem_safe_music_name(s_config.birthday_file)) {
        strlcpy(s_config.birthday_file, "birthday.wav", sizeof(s_config.birthday_file));
    }
    return ESP_OK;
}

void nvs_config_get(app_config_t *out)
{
    if (out == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_config;
    xSemaphoreGive(s_lock);
}

uint32_t nvs_config_revision(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint32_t value = s_revision;
    xSemaphoreGive(s_lock);
    return value;
}

const char *nvs_config_mode_name(app_mode_t mode)
{
    return mode == APP_MODE_RADIO ? "radio" : "birthday";
}

static bool json_string(const cJSON *root, const char *name, char *out, size_t size,
                        char *error, size_t error_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (item == NULL) return true;
    if (!cJSON_IsString(item) || item->valuestring == NULL || strlen(item->valuestring) >= size) {
        snprintf(error, error_size, "%s is invalid or too long", name);
        return false;
    }
    strlcpy(out, item->valuestring, size);
    return true;
}

esp_err_t nvs_config_update_json(const cJSON *json, char *error, size_t error_size)
{
    if (!cJSON_IsObject(json)) {
        snprintf(error, error_size, "JSON object required");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    app_config_t candidate = s_config;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, "mode");
    if (item != NULL) {
        if (!cJSON_IsString(item)) {
            snprintf(error, error_size, "mode must be birthday or radio");
            goto invalid;
        }
        if (strcmp(item->valuestring, "birthday") == 0) candidate.mode = APP_MODE_BIRTHDAY;
        else if (strcmp(item->valuestring, "radio") == 0) candidate.mode = APP_MODE_RADIO;
        else {
            snprintf(error, error_size, "mode must be birthday or radio");
            goto invalid;
        }
    }
    if (!json_string(json, "wifi_ssid", candidate.wifi_ssid, sizeof(candidate.wifi_ssid), error, error_size) ||
        !json_string(json, "wifi_password", candidate.wifi_password, sizeof(candidate.wifi_password), error, error_size) ||
        !json_string(json, "radio_url", candidate.radio_url, sizeof(candidate.radio_url), error, error_size) ||
        !json_string(json, "birthday_file", candidate.birthday_file, sizeof(candidate.birthday_file), error, error_size)) {
        goto invalid;
    }
    if (!filesystem_safe_music_name(candidate.birthday_file)) {
        snprintf(error, error_size, "birthday_file must be a valid wav or mp3 name");
        goto invalid;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "volume");
    if (item != NULL) {
        if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 100) {
            snprintf(error, error_size, "volume must be 0..100");
            goto invalid;
        }
        candidate.volume = (uint8_t)item->valueint;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "lux_threshold");
    if (item != NULL) {
        if (!cJSON_IsNumber(item) || item->valuedouble < 1 || item->valuedouble > 100000) {
            snprintf(error, error_size, "lux_threshold must be 1..100000");
            goto invalid;
        }
        candidate.lux_threshold = (float)item->valuedouble;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "birthday_count");
    if (item != NULL) {
        if (!cJSON_IsNumber(item) || item->valuedouble < 1 || item->valuedouble > 100) {
            snprintf(error, error_size, "birthday_count must be 1..100");
            goto invalid;
        }
        candidate.birthday_count = (uint16_t)item->valueint;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "play_on_boot");
    if (item != NULL) {
        if (!cJSON_IsBool(item)) {
            snprintf(error, error_size, "play_on_boot must be boolean");
            goto invalid;
        }
        candidate.play_on_boot = cJSON_IsTrue(item);
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "play_boot_loop");
    if (item != NULL) {
        if (!cJSON_IsBool(item)) {
            snprintf(error, error_size, "play_boot_loop must be boolean");
            goto invalid;
        }
        candidate.play_boot_loop = cJSON_IsTrue(item);
    }

    app_config_t previous = s_config;
    s_config = candidate;
    esp_err_t err = persist_locked();
    if (err == ESP_OK) {
        ++s_revision;
    } else {
        s_config = previous;
        snprintf(error, error_size, "NVS write failed: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) (void)nvs_config_write_mirror();
    return err;

invalid:
    xSemaphoreGive(s_lock);
    return ESP_ERR_INVALID_ARG;
}

cJSON *nvs_config_to_json(void)
{
    app_config_t config;
    nvs_config_get(&config);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mode", nvs_config_mode_name(config.mode));
    cJSON_AddStringToObject(root, "wifi_ssid", config.wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_password", config.wifi_password);
    cJSON_AddNumberToObject(root, "volume", config.volume);
    cJSON_AddNumberToObject(root, "lux_threshold", config.lux_threshold);
    cJSON_AddStringToObject(root, "radio_url", config.radio_url);
    cJSON_AddNumberToObject(root, "birthday_count", config.birthday_count);
    cJSON_AddStringToObject(root, "birthday_file", config.birthday_file);
    cJSON_AddBoolToObject(root, "play_on_boot", config.play_on_boot);
    cJSON_AddBoolToObject(root, "play_boot_loop", config.play_boot_loop);
    return root;
}

esp_err_t nvs_config_write_mirror(void)
{
    cJSON *json = nvs_config_to_json();
    char *text = cJSON_Print(json);
    cJSON_Delete(json);
    if (text == NULL) return ESP_ERR_NO_MEM;
    FILE *file = fopen(CONFIG_MIRROR_PATH, "wb");
    if (file == NULL) {
        cJSON_free(text);
        return ESP_FAIL;
    }
    size_t length = strlen(text);
    size_t written = fwrite(text, 1, length, file);
    fputc('\n', file);
    fclose(file);
    cJSON_free(text);
    ESP_LOGI(TAG, "configuration mirror updated");
    return written == length ? ESP_OK : ESP_FAIL;
}
