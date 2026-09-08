#include "i2s_player.h"

#include <limits.h>
#include <string.h>
#include "audio_element.h"
#include "audio_pipeline.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_ll.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "soc/gpio_struct.h"

#define I2S_BCLK_GPIO 4
#define I2S_WS_GPIO   5
#define I2S_DOUT_GPIO 6
#define CONVERSION_FRAMES 1024
#define I2S_OUTPUT_SAMPLE_RATE 48000
#define DIAGNOSTIC_SAMPLE_ATTEMPTS 8
#define SIGNAL_BCLK_BIT BIT0
#define SIGNAL_WS_BIT   BIT1
#define SIGNAL_DOUT_BIT BIT2

static const char *TAG = "i2s_player";
static audio_pipeline_handle_t s_pipeline;
static audio_element_handle_t s_raw_stream;
static audio_element_handle_t s_i2s_stream;
static uint32_t s_source_sample_rate;
static bool s_enabled;
static uint8_t s_volume = 80;
static int32_t *s_stereo_buffer;
static bool s_have_previous_sample;
static int32_t s_previous_sample;
static uint64_t s_input_frame_index;
static uint64_t s_output_frame_index;
static uint8_t s_signal_seen_high;
static uint8_t s_signal_seen_low;
static uint8_t s_diagnostic_attempts;

static void sample_output_signals(void)
{
    if (s_diagnostic_attempts >= DIAGNOSTIC_SAMPLE_ATTEMPTS) return;
    ++s_diagnostic_attempts;
    for (size_t i = 0; i < 2048; ++i) {
        uint8_t levels = 0;
        if (gpio_ll_get_level(&GPIO, I2S_BCLK_GPIO)) levels |= SIGNAL_BCLK_BIT;
        if (gpio_ll_get_level(&GPIO, I2S_WS_GPIO)) levels |= SIGNAL_WS_BIT;
        if (gpio_ll_get_level(&GPIO, I2S_DOUT_GPIO)) levels |= SIGNAL_DOUT_BIT;
        s_signal_seen_high |= levels;
        s_signal_seen_low |= (uint8_t)(~levels) &
                             (SIGNAL_BCLK_BIT | SIGNAL_WS_BIT | SIGNAL_DOUT_BIT);
    }
}

esp_err_t i2s_player_init(void)
{
    s_stereo_buffer = heap_caps_malloc(CONVERSION_FRAMES * 2 * sizeof(int32_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_stereo_buffer == NULL) {
        s_stereo_buffer = heap_caps_malloc(CONVERSION_FRAMES * 2 * sizeof(int32_t),
                                           MALLOC_CAP_8BIT);
    }
    if (s_stereo_buffer == NULL) return ESP_ERR_NO_MEM;

    audio_pipeline_cfg_t pipeline_config = DEFAULT_AUDIO_PIPELINE_CONFIG();
    s_pipeline = audio_pipeline_init(&pipeline_config);
    if (s_pipeline == NULL) return ESP_ERR_NO_MEM;

    raw_stream_cfg_t raw_config = RAW_STREAM_CFG_DEFAULT();
    raw_config.type = AUDIO_STREAM_WRITER;
    raw_config.out_rb_size = 32 * 1024;
    s_raw_stream = raw_stream_init(&raw_config);

    i2s_stream_cfg_t i2s_config =
        I2S_STREAM_CFG_DEFAULT_WITH_PARA(I2S_NUM_0, I2S_OUTPUT_SAMPLE_RATE,
                                         I2S_DATA_BIT_WIDTH_32BIT,
                                         AUDIO_STREAM_WRITER);
    i2s_config.chan_cfg.dma_desc_num = 16;
    i2s_config.chan_cfg.dma_frame_num = 512;
    i2s_config.chan_cfg.auto_clear = true;
    i2s_config.chan_cfg.intr_priority = 2;
    i2s_config.std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    i2s_config.std_cfg.gpio_cfg.bclk = I2S_BCLK_GPIO;
    i2s_config.std_cfg.gpio_cfg.ws = I2S_WS_GPIO;
    i2s_config.std_cfg.gpio_cfg.dout = I2S_DOUT_GPIO;
    i2s_config.std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    s_i2s_stream = i2s_stream_init(&i2s_config);
    if (s_raw_stream == NULL || s_i2s_stream == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = audio_pipeline_register(s_pipeline, s_raw_stream, "raw");
    if (err != ESP_OK) return err;
    err = audio_pipeline_register(s_pipeline, s_i2s_stream, "i2s");
    if (err != ESP_OK) return err;
    const char *links[] = {"raw", "i2s"};
    err = audio_pipeline_link(s_pipeline, links, 2);
    if (err != ESP_OK) return err;

    gpio_ll_input_enable(&GPIO, I2S_BCLK_GPIO);
    gpio_ll_input_enable(&GPIO, I2S_WS_GPIO);
    gpio_ll_input_enable(&GPIO, I2S_DOUT_GPIO);
    s_source_sample_rate = I2S_OUTPUT_SAMPLE_RATE;
    s_enabled = false;
    ESP_LOGI(TAG, "ESP-ADF output ready: %d Hz, 32-bit Philips, BCLK=%d WS=%d DIN=%d",
             I2S_OUTPUT_SAMPLE_RATE, I2S_BCLK_GPIO, I2S_WS_GPIO, I2S_DOUT_GPIO);
    return ESP_OK;
}

esp_err_t i2s_player_configure(uint32_t sample_rate)
{
    if (sample_rate < 8000 || sample_rate > 96000) return ESP_ERR_INVALID_ARG;
    if (s_enabled) return ESP_OK;

    s_source_sample_rate = sample_rate;
    s_have_previous_sample = false;
    s_previous_sample = 0;
    s_input_frame_index = 0;
    s_output_frame_index = 0;
    s_signal_seen_high = 0;
    s_signal_seen_low = 0;
    s_diagnostic_attempts = 0;

    (void)audio_pipeline_reset_ringbuffer(s_pipeline);
    (void)audio_pipeline_reset_elements(s_pipeline);
    (void)audio_pipeline_change_state(s_pipeline, AEL_STATE_INIT);
    esp_err_t err = audio_pipeline_run(s_pipeline);
    if (err == ESP_OK) s_enabled = true;
    return err;
}

void i2s_player_set_volume(uint8_t volume)
{
    s_volume = volume > 100 ? 100 : volume;
}

static int32_t pack_sample(int32_t sample)
{
    int32_t scaled = (sample * s_volume) / 100;
    if (scaled > INT16_MAX) scaled = INT16_MAX;
    if (scaled < INT16_MIN) scaled = INT16_MIN;
    return scaled * 65536;
}

static esp_err_t write_converted_frames(size_t frames)
{
    if (frames == 0) return ESP_OK;
    const size_t length = frames * 2 * sizeof(int32_t);
    size_t offset = 0;
    while (offset < length) {
        int written = raw_stream_write(s_raw_stream,
                                       (char *)s_stereo_buffer + offset,
                                       (int)(length - offset));
        if (written <= 0) return ESP_ERR_TIMEOUT;
        offset += (size_t)written;
    }
    sample_output_signals();
    return ESP_OK;
}

static esp_err_t append_output_sample(int32_t sample, size_t *output_frames)
{
    int32_t packed = pack_sample(sample);
    s_stereo_buffer[*output_frames * 2] = packed;
    s_stereo_buffer[*output_frames * 2 + 1] = packed;
    ++(*output_frames);
    if (*output_frames < CONVERSION_FRAMES) return ESP_OK;
    esp_err_t err = write_converted_frames(*output_frames);
    *output_frames = 0;
    return err;
}

esp_err_t i2s_player_write(const int16_t *samples, size_t sample_count, uint8_t channels)
{
    if (samples == NULL || sample_count == 0 || (channels != 1 && channels != 2)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_enabled) return ESP_ERR_INVALID_STATE;

    size_t input_frames = sample_count / channels;
    size_t output_frames = 0;
    for (size_t i = 0; i < input_frames; ++i) {
        int32_t current = channels == 1
                              ? samples[i]
                              : ((int32_t)samples[i * 2] + (int32_t)samples[i * 2 + 1]) / 2;

        if (!s_have_previous_sample) {
            esp_err_t err = append_output_sample(current, &output_frames);
            if (err != ESP_OK) return err;
            s_previous_sample = current;
            s_have_previous_sample = true;
            s_input_frame_index = 1;
            s_output_frame_index = 1;
            continue;
        }

        uint64_t interval_start = (s_input_frame_index - 1) * I2S_OUTPUT_SAMPLE_RATE;
        uint64_t interval_end = s_input_frame_index * I2S_OUTPUT_SAMPLE_RATE;
        while (s_output_frame_index * s_source_sample_rate <= interval_end) {
            uint64_t position = s_output_frame_index * s_source_sample_rate - interval_start;
            int32_t interpolated = s_previous_sample +
                                   (int32_t)(((int64_t)(current - s_previous_sample) *
                                              (int64_t)position) /
                                             I2S_OUTPUT_SAMPLE_RATE);
            esp_err_t err = append_output_sample(interpolated, &output_frames);
            if (err != ESP_OK) return err;
            ++s_output_frame_index;
        }
        s_previous_sample = current;
        ++s_input_frame_index;
    }
    return write_converted_frames(output_frames);
}

esp_err_t i2s_player_silence(void)
{
    if (!s_enabled) return ESP_OK;

    memset(s_stereo_buffer, 0, 256 * 2 * sizeof(int32_t));
    (void)raw_stream_write(s_raw_stream, (char *)s_stereo_buffer,
                           256 * 2 * sizeof(int32_t));
    vTaskDelay(pdMS_TO_TICKS(40));

    esp_err_t err = audio_pipeline_stop(s_pipeline);
    if (err == ESP_OK) {
        err = audio_pipeline_wait_for_stop_with_ticks(s_pipeline, pdMS_TO_TICKS(1000));
    }
    (void)audio_pipeline_terminate_with_ticks(s_pipeline, pdMS_TO_TICKS(1000));
    s_enabled = false;
    s_have_previous_sample = false;
    return err;
}

void i2s_player_get_signal_diagnostics(bool *bclk_toggled, bool *ws_toggled,
                                       bool *dout_toggled)
{
    uint8_t toggled = s_signal_seen_high & s_signal_seen_low;
    if (bclk_toggled != NULL) *bclk_toggled = (toggled & SIGNAL_BCLK_BIT) != 0;
    if (ws_toggled != NULL) *ws_toggled = (toggled & SIGNAL_WS_BIT) != 0;
    if (dout_toggled != NULL) *dout_toggled = (toggled & SIGNAL_DOUT_BIT) != 0;
}
