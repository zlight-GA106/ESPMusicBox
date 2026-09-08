#include "littlefs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "storage";
static SemaphoreHandle_t s_lock;
static FILE *s_upload;
static char s_upload_temp[320];
static char s_upload_final[320];
static size_t s_upload_expected;
static size_t s_upload_written;

static void ensure_directory(const char *path)
{
    struct stat info;
    if (stat(path, &info) != 0) {
        if (mkdir(path, 0775) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "mkdir(%s) failed: errno=%d", path, errno);
        }
    }
}

esp_err_t filesystem_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    const esp_vfs_littlefs_conf_t config = {
        .base_path = LITTLEFS_BASE_PATH,
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
        .grow_on_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&config);
    if (err != ESP_OK) return err;
    ensure_directory(MUSIC_DIRECTORY);
    ensure_directory(LITTLEFS_BASE_PATH "/config");
    size_t total = 0, used = 0;
    if (esp_littlefs_info("littlefs", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
    }
    return ESP_OK;
}

size_t filesystem_total_bytes(void)
{
    size_t total = 0, used = 0;
    (void)esp_littlefs_info("littlefs", &total, &used);
    return total;
}

size_t filesystem_used_bytes(void)
{
    size_t total = 0, used = 0;
    (void)esp_littlefs_info("littlefs", &total, &used);
    return used;
}

bool filesystem_safe_music_name(const char *name)
{
    if (name == NULL || name[0] == '\0' || strlen(name) > 96 || name[0] == '.') return false;
    for (const char *p = name; *p != '\0'; ++p) {
        const char c = *p;
        if (!(c == '.' || c == '-' || c == '_' ||
              (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
            return false;
        }
    }
    const char *extension = strrchr(name, '.');
    return extension != NULL && (strcasecmp(extension, ".wav") == 0 || strcasecmp(extension, ".mp3") == 0);
}

void filesystem_music_path(const char *name, char *out, size_t out_size)
{
    snprintf(out, out_size, MUSIC_DIRECTORY "/%s", name);
}

cJSON *filesystem_list_music(void)
{
    cJSON *array = cJSON_CreateArray();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    DIR *directory = opendir(MUSIC_DIRECTORY);
    if (directory != NULL) {
        struct dirent *entry;
        while ((entry = readdir(directory)) != NULL) {
            if (!filesystem_safe_music_name(entry->d_name)) continue;
            char path[320];
            filesystem_music_path(entry->d_name, path, sizeof(path));
            struct stat info;
            if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;
            cJSON *file = cJSON_CreateObject();
            cJSON_AddStringToObject(file, "name", entry->d_name);
            cJSON_AddNumberToObject(file, "size", (double)info.st_size);
            cJSON_AddItemToArray(array, file);
        }
        closedir(directory);
    }
    xSemaphoreGive(s_lock);
    return array;
}

esp_err_t filesystem_delete_music(const char *name)
{
    if (!filesystem_safe_music_name(name)) return ESP_ERR_INVALID_ARG;
    char path[320];
    filesystem_music_path(name, path, sizeof(path));
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int result = unlink(path);
    xSemaphoreGive(s_lock);
    if (result == 0) return ESP_OK;
    return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
}

esp_err_t filesystem_upload_begin(const char *name, size_t expected_size)
{
    if (!filesystem_safe_music_name(name) || expected_size == 0) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_upload != NULL) {
        fclose(s_upload);
        s_upload = NULL;
        (void)unlink(s_upload_temp);
    }
    filesystem_music_path(name, s_upload_final, sizeof(s_upload_final));
    if (strlen(s_upload_final) + sizeof(".upload") > sizeof(s_upload_temp)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_SIZE;
    }
    strlcpy(s_upload_temp, s_upload_final, sizeof(s_upload_temp));
    strlcat(s_upload_temp, ".upload", sizeof(s_upload_temp));
    s_upload = fopen(s_upload_temp, "wb");
    s_upload_expected = expected_size;
    s_upload_written = 0;
    esp_err_t err = s_upload == NULL ? ESP_FAIL : ESP_OK;
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t filesystem_upload_write(const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_upload == NULL || s_upload_written + length > s_upload_expected) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    size_t written = fwrite(data, 1, length, s_upload);
    s_upload_written += written;
    xSemaphoreGive(s_lock);
    return written == length ? ESP_OK : ESP_FAIL;
}

esp_err_t filesystem_upload_end(bool commit)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_upload == NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    bool complete = s_upload_written == s_upload_expected;
    bool io_ok = fflush(s_upload) == 0;
    if (fclose(s_upload) != 0) io_ok = false;
    s_upload = NULL;
    esp_err_t result = ESP_OK;
    if (commit && complete && io_ok) {
        (void)unlink(s_upload_final);
        if (rename(s_upload_temp, s_upload_final) != 0) result = ESP_FAIL;
    } else {
        (void)unlink(s_upload_temp);
        result = commit ? ESP_ERR_INVALID_SIZE : ESP_OK;
    }
    s_upload_expected = 0;
    s_upload_written = 0;
    xSemaphoreGive(s_lock);
    return result;
}

void storage_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(60000));
        size_t total = filesystem_total_bytes();
        size_t used = filesystem_used_bytes();
        ESP_LOGD(TAG, "storage health: %u free bytes", (unsigned)(total - used));
    }
}
