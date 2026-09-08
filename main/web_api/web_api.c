#include "web_api.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "app_state.h"
#include "app_version.h"
#include "audio_service.h"
#include "littlefs.h"
#include "nvs_config.h"
#include "wifi_manager.h"

static const char *TAG = "web_api";
static httpd_handle_t s_server;

static esp_err_t send_json(httpd_req_t *request, cJSON *json, const char *status)
{
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (text == NULL) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (status != NULL) httpd_resp_set_status(request, status);
    esp_err_t err = httpd_resp_sendstr(request, text);
    cJSON_free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *request, const char *status, const char *message)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", false);
    cJSON_AddStringToObject(json, "error", message);
    return send_json(request, json, status);
}

static cJSON *make_info(void)
{
    uint8_t mac[6] = {0};
    uint32_t flash_size = 0;
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    (void)esp_flash_get_size(NULL, &flash_size);
    char mac_text[18];
    snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "device_name", APP_DEVICE_NAME);
    cJSON_AddStringToObject(json, "mac", mac_text);
    cJSON_AddStringToObject(json, "firmware_version", APP_FIRMWARE_VERSION);
    cJSON_AddNumberToObject(json, "flash_size", flash_size);
    return json;
}

static cJSON *make_status(void)
{
    app_runtime_state_t state;
    app_state_get(&state);
    app_config_t config;
    nvs_config_get(&config);
    size_t total = filesystem_total_bytes();
    size_t used = filesystem_used_bytes();
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "lux", state.lux);
    cJSON_AddStringToObject(json, "mode", nvs_config_mode_name(config.mode));
    cJSON_AddStringToObject(json, "playback", app_state_playback_name(state.playback));
    cJSON_AddStringToObject(json, "playback_source", state.playback_source);
    cJSON_AddBoolToObject(json, "playback_loop", state.playback_loop);
    cJSON_AddBoolToObject(json, "wifi_connected", state.wifi_connected);
    cJSON_AddStringToObject(json, "ip_address", state.ip_address);
    cJSON_AddNumberToObject(json, "littlefs_total", (double)total);
    cJSON_AddNumberToObject(json, "littlefs_free", (double)(total >= used ? total - used : 0));
    cJSON_AddStringToObject(json, "last_error", state.last_error);
    return json;
}

static esp_err_t info_handler(httpd_req_t *request)
{
    return send_json(request, make_info(), NULL);
}

static esp_err_t status_handler(httpd_req_t *request)
{
    return send_json(request, make_status(), NULL);
}

static esp_err_t config_get_handler(httpd_req_t *request)
{
    return send_json(request, nvs_config_to_json(), NULL);
}

static cJSON *read_request_json(httpd_req_t *request)
{
    if (request->content_len == 0 || request->content_len > 2048) return NULL;
    char *body = malloc(request->content_len + 1);
    if (body == NULL) return NULL;
    size_t received = 0;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (count <= 0) {
            free(body);
            return NULL;
        }
        received += (size_t)count;
    }
    body[received] = '\0';
    cJSON *json = cJSON_Parse(body);
    free(body);
    return json;
}

static esp_err_t config_put_handler(httpd_req_t *request)
{
    cJSON *json = read_request_json(request);
    if (json == NULL) return send_error(request, "400 Bad Request", "invalid JSON");
    char error[96] = {0};
    esp_err_t err = nvs_config_update_json(json, error, sizeof(error));
    cJSON_Delete(json);
    if (err != ESP_OK) return send_error(request, "400 Bad Request", error);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", true);
    esp_err_t response_err = send_json(request, response, NULL);
    wifi_manager_notify_config_changed();
    return response_err;
}

static bool query_name(httpd_req_t *request, char *name, size_t size)
{
    size_t length = httpd_req_get_url_query_len(request);
    if (length == 0 || length >= 160) return false;
    char query[160];
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) return false;
    return httpd_query_key_value(query, "name", name, size) == ESP_OK &&
           filesystem_safe_music_name(name);
}

static esp_err_t files_get_handler(httpd_req_t *request)
{
    return send_json(request, filesystem_list_music(), NULL);
}

static esp_err_t files_post_handler(httpd_req_t *request)
{
    char name[97];
    if (!query_name(request, name, sizeof(name)) || request->content_len == 0) {
        return send_error(request, "400 Bad Request", "valid name and non-empty body required");
    }
    esp_err_t err = filesystem_upload_begin(name, request->content_len);
    if (err != ESP_OK) return send_error(request, "409 Conflict", esp_err_to_name(err));
    uint8_t buffer[1024];
    size_t remaining = request->content_len;
    while (remaining > 0) {
        size_t wanted = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        int count = httpd_req_recv(request, (char *)buffer, wanted);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (count <= 0 || filesystem_upload_write(buffer, (size_t)count) != ESP_OK) {
            (void)filesystem_upload_end(false);
            return send_error(request, "500 Internal Server Error", "upload failed");
        }
        remaining -= (size_t)count;
    }
    err = filesystem_upload_end(true);
    if (err != ESP_OK) return send_error(request, "500 Internal Server Error", esp_err_to_name(err));
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", true);
    return send_json(request, response, NULL);
}

static esp_err_t files_delete_handler(httpd_req_t *request)
{
    char name[97];
    if (!query_name(request, name, sizeof(name))) {
        return send_error(request, "400 Bad Request", "valid name required");
    }
    audio_service_stop();
    esp_err_t err = filesystem_delete_music(name);
    if (err != ESP_OK) return send_error(request, err == ESP_ERR_NOT_FOUND ? "404 Not Found" :
                                         "500 Internal Server Error", esp_err_to_name(err));
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", true);
    return send_json(request, response, NULL);
}

static esp_err_t play_handler(httpd_req_t *request)
{
    cJSON *json = read_request_json(request);
    const cJSON *name = json != NULL ? cJSON_GetObjectItemCaseSensitive(json, "name") : NULL;
    const cJSON *loop = json != NULL ? cJSON_GetObjectItemCaseSensitive(json, "loop") : NULL;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(name) && (loop == NULL || cJSON_IsBool(loop))) {
        err = audio_service_play_file(name->valuestring, cJSON_IsTrue(loop) ? 0 : 1);
    }
    cJSON_Delete(json);
    if (err != ESP_OK) return send_error(request, "400 Bad Request", esp_err_to_name(err));
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", true);
    return send_json(request, response, NULL);
}

static esp_err_t stop_handler(httpd_req_t *request)
{
    audio_service_stop();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", true);
    return send_json(request, response, NULL);
}

static esp_err_t ota_handler(httpd_req_t *request)
{
    return send_error(request, "501 Not Implemented", "OTA endpoint reserved");
}

esp_err_t web_api_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.stack_size = 6144;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri = "/api/info", .method = HTTP_GET, .handler = info_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/config", .method = HTTP_GET, .handler = config_get_handler},
        {.uri = "/api/config", .method = HTTP_PUT, .handler = config_put_handler},
        {.uri = "/api/files", .method = HTTP_GET, .handler = files_get_handler},
        {.uri = "/api/files", .method = HTTP_POST, .handler = files_post_handler},
        {.uri = "/api/files", .method = HTTP_DELETE, .handler = files_delete_handler},
        {.uri = "/api/play", .method = HTTP_POST, .handler = play_handler},
        {.uri = "/api/stop", .method = HTTP_POST, .handler = stop_handler},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = ota_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        if ((err = httpd_register_uri_handler(s_server, &routes[i])) != ESP_OK) {
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }
    ESP_LOGI(TAG, "HTTP API started on port %u", config.server_port);
    return ESP_OK;
}

void web_api_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
