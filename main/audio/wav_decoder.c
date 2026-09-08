#include "wav_decoder.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

bool wav_decoder_open(wav_decoder_t *decoder, const char *path)
{
    if (decoder == NULL || path == NULL) return false;
    memset(decoder, 0, sizeof(*decoder));
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;

    uint8_t riff[12];
    if (fread(riff, 1, sizeof(riff), file) != sizeof(riff) ||
        memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(file);
        return false;
    }

    bool format_found = false;
    uint16_t audio_format = 0;
    for (;;) {
        uint8_t chunk[8];
        if (fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) break;
        uint32_t chunk_size = read_u32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t format[40] = {0};
            if (chunk_size < 16) break;
            size_t format_bytes = chunk_size < sizeof(format) ? chunk_size : sizeof(format);
            if (fread(format, 1, format_bytes, file) != format_bytes) break;
            audio_format = read_u16(format);
            if (audio_format == 0xfffe && chunk_size >= 40) {
                audio_format = read_u16(format + 24);
            }
            decoder->audio_format = audio_format;
            decoder->channels = read_u16(format + 2);
            decoder->sample_rate = read_u32(format + 4);
            decoder->bits_per_sample = read_u16(format + 14);
            format_found = true;
            uint32_t remaining = chunk_size - (uint32_t)format_bytes;
            if (remaining > 0 && fseek(file, remaining, SEEK_CUR) != 0) break;
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (!format_found) break;
            decoder->data_remaining = chunk_size;
            decoder->file = file;
            bool pcm = audio_format == 1 &&
                       (decoder->bits_per_sample == 8 || decoder->bits_per_sample == 16 ||
                        decoder->bits_per_sample == 24 || decoder->bits_per_sample == 32);
            bool float32 = audio_format == 3 && decoder->bits_per_sample == 32;
            if ((pcm || float32) && (decoder->channels == 1 || decoder->channels == 2) &&
                decoder->sample_rate >= 8000 && decoder->sample_rate <= 96000) {
                return true;
            }
            break;
        } else if (fseek(file, chunk_size, SEEK_CUR) != 0) {
            break;
        }
        if ((chunk_size & 1U) != 0 && fseek(file, 1, SEEK_CUR) != 0) break;
    }
    fclose(file);
    memset(decoder, 0, sizeof(*decoder));
    return false;
}

size_t wav_decoder_read(wav_decoder_t *decoder, int16_t *samples, size_t max_samples)
{
    if (decoder == NULL || decoder->file == NULL || samples == NULL || max_samples == 0) return 0;
    if (decoder->bits_per_sample == 16) {
        size_t wanted = max_samples * sizeof(int16_t);
        if (wanted > decoder->data_remaining) wanted = decoder->data_remaining;
        wanted &= ~(size_t)1;
        size_t bytes = fread(samples, 1, wanted, decoder->file);
        decoder->data_remaining -= (uint32_t)bytes;
        return bytes / sizeof(int16_t);
    }

    size_t bytes_per_sample = decoder->bits_per_sample / 8;
    if (bytes_per_sample == 0) return 0;
    size_t wanted = max_samples;
    size_t available_samples = decoder->data_remaining / bytes_per_sample;
    if (wanted > available_samples) wanted = available_samples;
    size_t converted = 0;
    uint8_t temporary[384];
    while (converted < wanted) {
        size_t block_samples = wanted - converted;
        size_t max_block_samples = sizeof(temporary) / bytes_per_sample;
        if (block_samples > max_block_samples) block_samples = max_block_samples;
        size_t requested_bytes = block_samples * bytes_per_sample;
        size_t bytes = fread(temporary, 1, requested_bytes, decoder->file);
        size_t read_samples = bytes / bytes_per_sample;
        for (size_t i = 0; i < read_samples; ++i) {
            const uint8_t *source = temporary + i * bytes_per_sample;
            if (decoder->audio_format == 3) {
                float value;
                memcpy(&value, source, sizeof(value));
                if (value > 1.0f) value = 1.0f;
                if (value < -1.0f) value = -1.0f;
                samples[converted + i] = (int16_t)(value * 32767.0f);
            } else if (decoder->bits_per_sample == 8) {
                samples[converted + i] = (int16_t)(((int32_t)source[0] - 128) << 8);
            } else if (decoder->bits_per_sample == 24) {
                int32_t value = (int32_t)source[0] | ((int32_t)source[1] << 8) |
                                ((int32_t)source[2] << 16);
                if ((value & 0x00800000) != 0) value |= (int32_t)0xff000000;
                samples[converted + i] = (int16_t)(value >> 8);
            } else {
                int32_t value = (int32_t)((uint32_t)source[0] | ((uint32_t)source[1] << 8) |
                                          ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24));
                samples[converted + i] = (int16_t)(value >> 16);
            }
        }
        converted += read_samples;
        if (bytes < requested_bytes) break;
    }
    decoder->data_remaining -= (uint32_t)(converted * bytes_per_sample);
    return converted;
}

void wav_decoder_close(wav_decoder_t *decoder)
{
    if (decoder != NULL && decoder->file != NULL) fclose(decoder->file);
    if (decoder != NULL) memset(decoder, 0, sizeof(*decoder));
}
