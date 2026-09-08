#include "sine_test.h"

#include <math.h>
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "hal/gpio_ll.h"
#include "nvs.h"
#include "soc/gpio_struct.h"

#define SINE_BCLK_GPIO 4
#define SINE_WS_GPIO   5
#define SINE_DOUT_GPIO 6
#define SINE_RATE      48000
#define SINE_FREQ      1000.0f
#define SINE_PI        3.14159265f
#define SINE_FRAMES_PER_CHUNK 512

static const char *TAG = "sine_test";
static const char *NVS_NAMESPACE = "musicbox";
static const char *NVS_KEY_SECONDS = "sine_sec";
static const char *NVS_KEY_BITS = "sine_bits";

esp_err_t sine_test_request(uint32_t seconds, uint16_t bits_per_sample)
{
    if (seconds == 0 || (bits_per_sample != 16 && bits_per_sample != 32)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(handle, NVS_KEY_SECONDS, seconds);
    if (err == ESP_OK) err = nvs_set_u16(handle, NVS_KEY_BITS, bits_per_sample);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t clear_pending(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, NVS_KEY_SECONDS);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_erase_key(handle, NVS_KEY_BITS);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void run_tone(uint32_t seconds, uint16_t bits)
{
    ESP_LOGI(TAG, "1 kHz sine, %u-bit slots, %u s, BCLK=%d WS=%d DIN=%d",
             bits, (unsigned)seconds, SINE_BCLK_GPIO, SINE_WS_GPIO, SINE_DOUT_GPIO);

    i2s_chan_handle_t channel = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 512;
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, &channel, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SINE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        bits == 32 ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SINE_BCLK_GPIO,
            .ws = SINE_WS_GPIO,
            .dout = SINE_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    err = i2s_channel_init_std_mode(channel, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode: %s", esp_err_to_name(err));
        i2s_del_channel(channel);
        return;
    }
    err = i2s_channel_enable(channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable: %s", esp_err_to_name(err));
        i2s_del_channel(channel);
        return;
    }

    gpio_ll_input_enable(&GPIO, SINE_BCLK_GPIO);
    gpio_ll_input_enable(&GPIO, SINE_WS_GPIO);
    gpio_ll_input_enable(&GPIO, SINE_DOUT_GPIO);
    uint8_t seen_high = 0;
    uint8_t seen_low = 0;
    const uint8_t pin_bits = BIT0 | BIT1 | BIT2;

    int32_t buffer32[SINE_FRAMES_PER_CHUNK * 2];
    int16_t buffer16[SINE_FRAMES_PER_CHUNK * 2];
    const size_t bytes_per_frame = (size_t)bits / 8 * 2;
    uint64_t phase = 0;
    uint64_t total_frames = (uint64_t)seconds * SINE_RATE;
    while (phase < total_frames) {
        size_t frames = SINE_FRAMES_PER_CHUNK;
        if ((uint64_t)frames > total_frames - phase) frames = (size_t)(total_frames - phase);
        for (size_t i = 0; i < frames; ++i) {
            float angle = 2.0f * SINE_PI * SINE_FREQ * (float)(phase + i) / (float)SINE_RATE;
            int16_t value = (int16_t)(sinf(angle) * 10000.0f);
            if (bits == 32) {
                int32_t packed = (int32_t)value << 16;
                buffer32[i * 2] = packed;
                buffer32[i * 2 + 1] = packed;
            } else {
                buffer16[i * 2] = value;
                buffer16[i * 2 + 1] = value;
            }
        }
        size_t bytes_written = 0;
        const void *data = bits == 32 ? (const void *)buffer32 : (const void *)buffer16;
        err = i2s_channel_write(channel, data, frames * bytes_per_frame, &bytes_written,
                                pdMS_TO_TICKS(2000));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write: %s", esp_err_to_name(err));
            break;
        }
        for (size_t i = 0; i < 4096; ++i) {
            uint8_t levels = 0;
            if (gpio_ll_get_level(&GPIO, SINE_BCLK_GPIO)) levels |= BIT0;
            if (gpio_ll_get_level(&GPIO, SINE_WS_GPIO)) levels |= BIT1;
            if (gpio_ll_get_level(&GPIO, SINE_DOUT_GPIO)) levels |= BIT2;
            seen_high |= levels;
            seen_low |= (uint8_t)(~levels) & pin_bits;
        }
        phase += frames;
    }

    i2s_channel_disable(channel);
    i2s_del_channel(channel);
    uint8_t toggled = seen_high & seen_low;
    ESP_LOGI(TAG, "pins toggled: bclk=%d ws=%d dout=%d",
             (toggled & BIT0) != 0, (toggled & BIT1) != 0, (toggled & BIT2) != 0);
    ESP_LOGI(TAG, "sine test finished");
}

void sine_test_run_if_pending(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG, "nvs_open: %s", esp_err_to_name(err));
        return;
    }
    uint32_t seconds = 0;
    uint16_t bits = 16;
    err = nvs_get_u32(handle, NVS_KEY_SECONDS, &seconds);
    if (err == ESP_OK) {
        (void)nvs_get_u16(handle, NVS_KEY_BITS, &bits);
    }
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return;
    if (err != ESP_OK) return;

    (void)clear_pending();
    if (seconds == 0 || seconds > 60 || (bits != 16 && bits != 32)) {
        ESP_LOGW(TAG, "invalid pending parameters, test skipped");
        return;
    }
    run_tone(seconds, bits);
}
