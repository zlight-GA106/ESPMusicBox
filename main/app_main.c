#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_state.h"
#include "audio_service.h"
#include "bh1750.h"
#include "littlefs.h"
#include "nvs_config.h"
#include "sine_test.h"
#include "usb_serial.h"
#include "web_api.h"
#include "wifi_manager.h"

static const char *TAG = "app_main";

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    initialize_nvs();
    sine_test_run_if_pending();
    app_state_init();
    ESP_ERROR_CHECK(nvs_config_init());
    ESP_ERROR_CHECK(filesystem_init());
    (void)nvs_config_write_mirror();
    ESP_ERROR_CHECK(audio_service_init());
    esp_err_t sensor_err = bh1750_init();
    if (sensor_err != ESP_OK) {
        ESP_LOGW(TAG, "BH1750 unavailable (%s); sensor task will retry",
                 esp_err_to_name(sensor_err));
    }
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(web_api_start());
    ESP_ERROR_CHECK(usb_serial_init());

    xTaskCreate(audio_task, "audio_task", 8192, NULL, 6, NULL);
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 4, NULL);
    xTaskCreate(wifi_task, "wifi_task", 4096, NULL, 4, NULL);
    xTaskCreate(config_task, "config_task", 16384, NULL, 5, NULL);
    xTaskCreate(storage_task, "storage_task", 3072, NULL, 2, NULL);
    app_config_t config;
    nvs_config_get(&config);
    if (config.mode == APP_MODE_BIRTHDAY && config.play_on_boot) {
        uint16_t count = config.play_boot_loop ? 0 : config.birthday_count;
        esp_err_t play_err = audio_service_play_birthday(count);
        if (play_err != ESP_OK) {
            ESP_LOGW(TAG, "power-on playback could not start: %s", esp_err_to_name(play_err));
        }
    }
    ESP_LOGI(TAG, "ESP32-S3 music box started");
}
