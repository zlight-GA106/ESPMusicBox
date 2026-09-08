#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "app_state.h"
#include "audio_service.h"
#include "nvs_config.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "wifi";
static EventGroupHandle_t s_events;
static TaskHandle_t s_task;
static bool s_started;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        app_state_set_wifi(false, "");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        char address[16];
        snprintf(address, sizeof(address), IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        app_state_set_wifi(true, address);
        ESP_LOGI(TAG, "connected, IP=%s", address);
    }
}

static esp_err_t apply_credentials(const app_config_t *config)
{
    if (s_started) {
        (void)esp_wifi_disconnect();
        (void)esp_wifi_stop();
        s_started = false;
    }
    xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
    app_state_set_wifi(false, "");
    if (config->wifi_ssid[0] == '\0') return ESP_OK;

    wifi_config_t station = {0};
    strlcpy((char *)station.sta.ssid, config->wifi_ssid, sizeof(station.sta.ssid));
    strlcpy((char *)station.sta.password, config->wifi_password, sizeof(station.sta.password));
    station.sta.threshold.authmode = config->wifi_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    station.sta.pmf_cfg.capable = true;
    station.sta.pmf_cfg.required = false;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &station);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) {
        s_started = true;
        err = esp_wifi_connect();
    }
    return err;
}

esp_err_t wifi_manager_init(void)
{
    s_events = xEventGroupCreate();
    if (s_events == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&initialization)) != ESP_OK) return err;
    if ((err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL)) != ESP_OK) return err;
    if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL)) != ESP_OK) return err;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) return err;
    return ESP_OK;
}

void wifi_manager_notify_config_changed(void)
{
    if (s_task != NULL) xTaskNotifyGive(s_task);
}

bool wifi_manager_is_connected(void)
{
    return s_events != NULL && (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT) != 0;
}

void wifi_task(void *arg)
{
    (void)arg;
    s_task = xTaskGetCurrentTaskHandle();
    app_config_t previous = {0};
    bool first = true;
    for (;;) {
        app_config_t current;
        nvs_config_get(&current);
        bool credentials_changed = first || strcmp(current.wifi_ssid, previous.wifi_ssid) != 0 ||
                                   strcmp(current.wifi_password, previous.wifi_password) != 0;
        bool source_changed = first || current.mode != previous.mode ||
                              strcmp(current.radio_url, previous.radio_url) != 0;
        if (credentials_changed) {
            esp_err_t err = apply_credentials(&current);
            if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi configuration failed: %s", esp_err_to_name(err));
        }
        audio_service_set_volume(current.volume);
        if (!first && source_changed) audio_service_stop();

        if (current.mode == APP_MODE_RADIO && wifi_manager_is_connected() &&
            current.radio_url[0] != '\0' && !audio_service_is_active()) {
            (void)audio_service_start_radio(current.radio_url);
        } else if (s_started && !wifi_manager_is_connected()) {
            (void)esp_wifi_connect();
        }
        previous = current;
        first = false;
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
    }
}
