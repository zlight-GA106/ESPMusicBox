#include "mp3_decoder.h"

#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "mp3dec.h"
#include "app_state.h"
#include "i2s_player.h"

#define MP3_INPUT_BUFFER_SIZE 16384
#define MP3_PCM_SAMPLES_MAX   2304
#define MP3_REFILL_THRESHOLD  4096

static const char *TAG = "mp3_decoder";

static void *audio_alloc(size_t size)
{
    void *memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory != NULL ? memory : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

esp_err_t mp3_decode_stream(mp3_input_read_fn read_fn,
                            void *read_context,
                            mp3_stop_fn should_stop,
                            void *stop_context)
{
    if (read_fn == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t *input = audio_alloc(MP3_INPUT_BUFFER_SIZE);
    int16_t *pcm = audio_alloc(MP3_PCM_SAMPLES_MAX * sizeof(int16_t));
    HMP3Decoder decoder = MP3InitDecoder();
    if (input == NULL || pcm == NULL || decoder == NULL) {
        free(input);
        free(pcm);
        if (decoder != NULL) MP3FreeDecoder(decoder);
        return ESP_ERR_NO_MEM;
    }

    uint8_t *read_pointer = input;
    int bytes_left = 0;
    bool end_of_input = false;
    bool decoded_frame = false;
    esp_err_t result = ESP_OK;

    for (;;) {
        if (should_stop != NULL && should_stop(stop_context)) break;
        if (!end_of_input && bytes_left < MP3_REFILL_THRESHOLD) {
            if (bytes_left > 0 && read_pointer != input) memmove(input, read_pointer, (size_t)bytes_left);
            read_pointer = input;
            int received = read_fn(read_context, input + bytes_left,
                                   MP3_INPUT_BUFFER_SIZE - (size_t)bytes_left);
            if (received < 0) {
                result = ESP_FAIL;
                break;
            }
            if (received == 0) end_of_input = true;
            else bytes_left += received;
        }
        if (bytes_left == 0 && end_of_input) break;

        int offset = MP3FindSyncWord(read_pointer, bytes_left);
        if (offset < 0) {
            if (end_of_input) break;
            int keep = bytes_left > 3 ? 3 : bytes_left;
            if (keep > 0) memmove(input, read_pointer + bytes_left - keep, (size_t)keep);
            read_pointer = input;
            bytes_left = keep;
            continue;
        }
        read_pointer += offset;
        bytes_left -= offset;
        int before = bytes_left;
        int status = MP3Decode(decoder, &read_pointer, &bytes_left, pcm, 0);
        if (status == ERR_MP3_INDATA_UNDERFLOW || status == ERR_MP3_MAINDATA_UNDERFLOW) {
            if (end_of_input) break;
            if (bytes_left == before && bytes_left > 0) {
                memmove(input, read_pointer, (size_t)bytes_left);
                read_pointer = input;
            }
            continue;
        }
        if (status != ERR_MP3_NONE) {
            ESP_LOGD(TAG, "recoverable decoder error %d", status);
            if (bytes_left == before && bytes_left > 0) {
                ++read_pointer;
                --bytes_left;
            }
            continue;
        }

        MP3FrameInfo info;
        MP3GetLastFrameInfo(decoder, &info);
        if ((info.nChans != 1 && info.nChans != 2) || info.outputSamps <= 0 ||
            info.outputSamps > MP3_PCM_SAMPLES_MAX || info.bitsPerSample != 16) {
            result = ESP_ERR_NOT_SUPPORTED;
            break;
        }
        result = i2s_player_configure((uint32_t)info.samprate);
        if (result != ESP_OK) break;
        if (!decoded_frame) {
            app_state_set_playback(PLAYBACK_PLAYING, NULL);
            decoded_frame = true;
        }
        result = i2s_player_write(pcm, (size_t)info.outputSamps, (uint8_t)info.nChans);
        if (result != ESP_OK) break;
    }

    MP3FreeDecoder(decoder);
    free(input);
    free(pcm);
    if (!decoded_frame && result == ESP_OK && !(should_stop != NULL && should_stop(stop_context))) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    return result;
}
