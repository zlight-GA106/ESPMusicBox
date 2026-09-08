#include "usb_serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "app_state.h"
#include "app_version.h"
#include "audio_service.h"
#include "i2s_player.h"
#include "littlefs.h"
#include "nvs_config.h"
#include "sine_test.h"
#include "wifi_manager.h"

#define USB_LINE_MAX 4096
#define CONFIG_UART UART_NUM_0
#define CONFIG_UART_TX_GPIO 43
#define CONFIG_UART_RX_GPIO 44

typedef enum {
    CONFIG_TRANSPORT_USB,
    CONFIG_TRANSPORT_UART,
} config_transport_t;

static const char *TAG = "usb_serial";
static StreamBufferHandle_t s_receive_stream;
static QueueHandle_t s_uart_line_queue;
static esp_ota_handle_t s_ota_handle;

static void uart_rx_task(void *arg);
static const esp_partition_t *s_ota_partition;
static size_t s_ota_expected;
static size_t s_ota_written;
static bool s_ota_active;

static void receive_callback(int interface, cdcacm_event_t *event)
{
    (void)event;
    uint8_t buffer[256];
    size_t count;
    do {
        count = 0;
        if (tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)interface, buffer, sizeof(buffer), &count) == ESP_OK &&
            count > 0) {
            (void)xStreamBufferSend(s_receive_stream, buffer, count, 0);
        }
    } while (count == sizeof(buffer));
}

static void write_all(config_transport_t transport, const char *text)
{
    size_t length = strlen(text);
    if (transport == CONFIG_TRANSPORT_UART) {
        (void)uart_write_bytes(CONFIG_UART, text, length);
        (void)uart_write_bytes(CONFIG_UART, "\n", 1);
        (void)uart_wait_tx_done(CONFIG_UART, pdMS_TO_TICKS(1000));
        return;
    }
    size_t offset = 0;
    while (offset < length) {
        size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,
                                                   (const uint8_t *)text + offset,
                                                   length - offset);
        offset += queued;
        (void)tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
        if (queued == 0) vTaskDelay(pdMS_TO_TICKS(2));
    }
    (void)tinyusb_cdcacm_write_queue_char(TINYUSB_CDC_ACM_0, '\n');
    (void)tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(100));
}

static cJSON *response_base(const cJSON *request, bool ok)
{
    cJSON *response = cJSON_CreateObject();
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(request, "id");
    if (id != NULL) cJSON_AddItemToObject(response, "id", cJSON_Duplicate(id, true));
    else cJSON_AddNullToObject(response, "id");
    cJSON_AddBoolToObject(response, "ok", ok);
    return response;
}

static void send_response(config_transport_t transport, cJSON *response)
{
    char *text = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (text != NULL) {
        write_all(transport, text);
        cJSON_free(text);
    }
}

static void send_error(config_transport_t transport, const cJSON *request, const char *message)
{
    cJSON *response = response_base(request, false);
    cJSON_AddStringToObject(response, "error", message);
    send_response(transport, response);
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
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device_name", APP_DEVICE_NAME);
    cJSON_AddStringToObject(data, "mac", mac_text);
    cJSON_AddStringToObject(data, "firmware_version", APP_FIRMWARE_VERSION);
    cJSON_AddNumberToObject(data, "flash_size", flash_size);
    return data;
}

static cJSON *make_status(void)
{
    app_runtime_state_t state;
    app_state_get(&state);
    app_config_t config;
    nvs_config_get(&config);
    size_t total = filesystem_total_bytes();
    size_t used = filesystem_used_bytes();
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "lux", state.lux);
    cJSON_AddStringToObject(data, "mode", nvs_config_mode_name(config.mode));
    cJSON_AddStringToObject(data, "playback", app_state_playback_name(state.playback));
    cJSON_AddStringToObject(data, "playback_source", state.playback_source);
    cJSON_AddBoolToObject(data, "playback_loop", state.playback_loop);
    cJSON_AddBoolToObject(data, "wifi_connected", state.wifi_connected);
    cJSON_AddStringToObject(data, "ip_address", state.ip_address);
    cJSON_AddNumberToObject(data, "littlefs_total", (double)total);
    cJSON_AddNumberToObject(data, "littlefs_free", (double)(total >= used ? total - used : 0));
    cJSON_AddStringToObject(data, "last_error", state.last_error);
    return data;
}

static const cJSON *request_data(const cJSON *request)
{
    return cJSON_GetObjectItemCaseSensitive(request, "data");
}

static const cJSON *data_item(const cJSON *request, const char *name)
{
    const cJSON *data = request_data(request);
    return cJSON_IsObject(data) ? cJSON_GetObjectItemCaseSensitive(data, name) : NULL;
}

static void process_request(const char *line, config_transport_t transport)
{
    bool restart_after_response = false;
    cJSON *request = cJSON_Parse(line);
    if (request == NULL) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddNullToObject(response, "id");
        cJSON_AddBoolToObject(response, "ok", false);
        cJSON_AddStringToObject(response, "error", "invalid JSON");
        send_response(transport, response);
        return;
    }
    const cJSON *command = cJSON_GetObjectItemCaseSensitive(request, "cmd");
    if (!cJSON_IsString(command)) {
        send_error(transport, request, "cmd is required");
        cJSON_Delete(request);
        return;
    }

    cJSON *response = response_base(request, true);
    esp_err_t err = ESP_OK;
    if (strcmp(command->valuestring, "ping") == 0) {
        cJSON_AddStringToObject(response, "data", "pong");
    } else if (strcmp(command->valuestring, "info.get") == 0) {
        cJSON_AddItemToObject(response, "data", make_info());
    } else if (strcmp(command->valuestring, "status.get") == 0) {
        cJSON_AddItemToObject(response, "data", make_status());
    } else if (strcmp(command->valuestring, "config.get") == 0) {
        cJSON_AddItemToObject(response, "data", nvs_config_to_json());
    } else if (strcmp(command->valuestring, "config.set") == 0) {
        char error[96] = {0};
        err = nvs_config_update_json(request_data(request), error, sizeof(error));
        if (err != ESP_OK) {
            cJSON_Delete(response);
            send_error(transport, request, error);
            cJSON_Delete(request);
            return;
        }
        wifi_manager_notify_config_changed();
    } else if (strcmp(command->valuestring, "file.list") == 0) {
        cJSON_AddItemToObject(response, "data", filesystem_list_music());
    } else if (strcmp(command->valuestring, "file.begin") == 0) {
        const cJSON *name = data_item(request, "name");
        const cJSON *size = data_item(request, "size");
        if (!cJSON_IsString(name) || !cJSON_IsNumber(size) || size->valuedouble <= 0) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = filesystem_upload_begin(name->valuestring, (size_t)size->valuedouble);
        }
    } else if (strcmp(command->valuestring, "file.chunk") == 0) {
        const cJSON *encoded = data_item(request, "base64");
        uint8_t decoded[3072];
        size_t decoded_length = 0;
        if (!cJSON_IsString(encoded) ||
            mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_length,
                                  (const unsigned char *)encoded->valuestring,
                                  strlen(encoded->valuestring)) != 0 || decoded_length == 0) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = filesystem_upload_write(decoded, decoded_length);
        }
    } else if (strcmp(command->valuestring, "file.end") == 0) {
        err = filesystem_upload_end(true);
    } else if (strcmp(command->valuestring, "file.abort") == 0) {
        err = filesystem_upload_end(false);
    } else if (strcmp(command->valuestring, "file.delete") == 0) {
        const cJSON *name = data_item(request, "name");
        if (!cJSON_IsString(name)) err = ESP_ERR_INVALID_ARG;
        else {
            audio_service_stop();
            err = filesystem_delete_music(name->valuestring);
        }
    } else if (strcmp(command->valuestring, "audio.play") == 0) {
        const cJSON *name = data_item(request, "name");
        const cJSON *loop = data_item(request, "loop");
        if (!cJSON_IsString(name) || (loop != NULL && !cJSON_IsBool(loop))) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = audio_service_play_file(name->valuestring, cJSON_IsTrue(loop) ? 0 : 1);
        }
    } else if (strcmp(command->valuestring, "audio.stop") == 0) {
        audio_service_stop();
    } else if (strcmp(command->valuestring, "audio.diagnostics") == 0) {
        bool bclk_toggled = false;
        bool ws_toggled = false;
        bool dout_toggled = false;
        i2s_player_get_signal_diagnostics(&bclk_toggled, &ws_toggled, &dout_toggled);
        cJSON *data = cJSON_CreateObject();
        cJSON_AddBoolToObject(data, "bclk_toggled", bclk_toggled);
        cJSON_AddBoolToObject(data, "ws_toggled", ws_toggled);
        cJSON_AddBoolToObject(data, "dout_toggled", dout_toggled);
        cJSON_AddItemToObject(response, "data", data);
    } else if (strcmp(command->valuestring, "audio.sine") == 0) {
        const cJSON *seconds = data_item(request, "seconds");
        const cJSON *bits = data_item(request, "bits");
        uint32_t run_seconds = cJSON_IsNumber(seconds) ? (uint32_t)seconds->valuedouble : 5;
        uint16_t run_bits = cJSON_IsNumber(bits) ? (uint16_t)bits->valuedouble : 16;
        if (run_seconds < 1 || run_seconds > 60 || (run_bits != 16 && run_bits != 32)) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            audio_service_stop();
            err = sine_test_request(run_seconds, run_bits);
            if (err == ESP_OK) restart_after_response = true;
        }
    } else if (strcmp(command->valuestring, "uart.baud") == 0) {
        const cJSON *baud = data_item(request, "baud");
        int rate = cJSON_IsNumber(baud) ? baud->valueint : 0;
        if (transport != CONFIG_TRANSPORT_UART) {
            err = ESP_ERR_INVALID_STATE;
        } else if (rate != 115200 && rate != 230400 && rate != 460800 && rate != 921600 &&
                   rate != 1500000 && rate != 2000000) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = uart_set_baudrate(CONFIG_UART, rate);
            if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
        }
    } else if (strcmp(command->valuestring, "ota.begin") == 0) {
        const cJSON *size = data_item(request, "size");
        if (s_ota_active || !cJSON_IsNumber(size) || size->valuedouble <= 0) {
            err = ESP_ERR_INVALID_STATE;
        } else {
            s_ota_partition = esp_ota_get_next_update_partition(NULL);
            s_ota_expected = (size_t)size->valuedouble;
            s_ota_written = 0;
            if (s_ota_partition == NULL || s_ota_expected > s_ota_partition->size) {
                err = ESP_ERR_INVALID_SIZE;
            } else {
                audio_service_stop();
                err = esp_ota_begin(s_ota_partition, s_ota_expected, &s_ota_handle);
                s_ota_active = err == ESP_OK;
            }
        }
    } else if (strcmp(command->valuestring, "ota.chunk") == 0) {
        const cJSON *encoded = data_item(request, "base64");
        uint8_t decoded[3072];
        size_t decoded_length = 0;
        if (!s_ota_active || !cJSON_IsString(encoded) ||
            mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_length,
                                  (const unsigned char *)encoded->valuestring,
                                  strlen(encoded->valuestring)) != 0 || decoded_length == 0 ||
            s_ota_written + decoded_length > s_ota_expected) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = esp_ota_write(s_ota_handle, decoded, decoded_length);
            if (err == ESP_OK) s_ota_written += decoded_length;
        }
    } else if (strcmp(command->valuestring, "ota.end") == 0) {
        if (!s_ota_active || s_ota_written != s_ota_expected) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            esp_ota_handle_t handle = s_ota_handle;
            const esp_partition_t *partition = s_ota_partition;
            s_ota_active = false;
            s_ota_handle = 0;
            s_ota_partition = NULL;
            s_ota_expected = 0;
            s_ota_written = 0;
            err = esp_ota_end(handle);
            if (err == ESP_OK) err = esp_ota_set_boot_partition(partition);
            restart_after_response = err == ESP_OK;
        }
    } else if (strcmp(command->valuestring, "ota.abort") == 0) {
        if (s_ota_active) (void)esp_ota_abort(s_ota_handle);
        s_ota_active = false;
        s_ota_handle = 0;
        s_ota_partition = NULL;
        s_ota_expected = 0;
        s_ota_written = 0;
    } else {
        cJSON_Delete(response);
        send_error(transport, request, "unknown command");
        cJSON_Delete(request);
        return;
    }

    if (err != ESP_OK) {
        cJSON_Delete(response);
        send_error(transport, request, esp_err_to_name(err));
    } else {
        send_response(transport, response);
    }
    cJSON_Delete(request);
    if (restart_after_response) {
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
    }
}

esp_err_t usb_serial_init(void)
{
    s_receive_stream = xStreamBufferCreate(16384, 1);
    if (s_receive_stream == NULL) return ESP_ERR_NO_MEM;
    s_uart_line_queue = xQueueCreate(32, sizeof(char *));
    if (s_uart_line_queue == NULL) return ESP_ERR_NO_MEM;
    const tinyusb_config_t usb_config = {0};
    esp_err_t err = tinyusb_driver_install(&usb_config);
    if (err != ESP_OK) return err;
    const tinyusb_config_cdcacm_t cdc_config = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = receive_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    err = tusb_cdc_acm_init(&cdc_config);
    if (err != ESP_OK) return err;

    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if ((err = uart_param_config(CONFIG_UART, &uart_config)) != ESP_OK) return err;
    if ((err = uart_set_pin(CONFIG_UART, CONFIG_UART_TX_GPIO, CONFIG_UART_RX_GPIO,
                            UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)) != ESP_OK) return err;
    if (!uart_is_driver_installed(CONFIG_UART)) {
        err = uart_driver_install(CONFIG_UART, 16384, 0, 0, NULL, 0);
        if (err != ESP_OK) return err;
    }
    ESP_LOGI(TAG, "configuration channels ready: USB CDC and UART0 TX=%d RX=%d",
             CONFIG_UART_TX_GPIO, CONFIG_UART_RX_GPIO);
    /* UART0 is also the CH340 protocol channel; keep runtime logs from mixing with JSON. */
    esp_log_level_set("*", ESP_LOG_NONE);
    if (xTaskCreate(uart_rx_task, "uart_rx_task", 8192, NULL, 8, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return err;
}

static void consume_bytes(char *line, size_t *used, const uint8_t *input, size_t count,
                          config_transport_t transport)
{
    for (size_t i = 0; i < count; ++i) {
        char c = (char)input[i];
        if (c == '\n') {
            if (*used > 0) {
                line[*used] = '\0';
                process_request(line, transport);
                *used = 0;
            }
        } else if (c != '\r') {
            if (*used < USB_LINE_MAX - 1) line[(*used)++] = c;
            else *used = 0;
        }
    }
}

static void uart_rx_task(void *arg)
{
    (void)arg;
    uint8_t input[512];
    char line[USB_LINE_MAX];
    size_t used = 0;
    for (;;) {
        int count = uart_read_bytes(CONFIG_UART, input, sizeof(input), pdMS_TO_TICKS(10));
        for (int i = 0; i < count; ++i) {
            char c = (char)input[i];
            if (c == '\n') {
                if (used > 0) {
                    char *copy = heap_caps_malloc(used + 1, MALLOC_CAP_8BIT);
                    if (copy != NULL) {
                        memcpy(copy, line, used);
                        copy[used] = '\0';
                        if (xQueueSend(s_uart_line_queue, &copy, pdMS_TO_TICKS(100)) != pdTRUE) {
                            free(copy);
                        }
                    }
                    used = 0;
                }
            } else if (c != '\r') {
                if (used < USB_LINE_MAX - 1) line[used++] = c;
                else used = 0;
            }
        }
    }
}

void config_task(void *arg)
{
    (void)arg;
    char usb_line[USB_LINE_MAX];
    size_t usb_used = 0;
    uint8_t input[256];
    for (;;) {
        size_t usb_count = xStreamBufferReceive(s_receive_stream, input, sizeof(input),
                                                pdMS_TO_TICKS(2));
        if (usb_count > 0) {
            consume_bytes(usb_line, &usb_used, input, usb_count, CONFIG_TRANSPORT_USB);
        }
        char *line;
        while (xQueueReceive(s_uart_line_queue, &line, 0) == pdTRUE) {
            process_request(line, CONFIG_TRANSPORT_UART);
            free(line);
        }
    }
}
