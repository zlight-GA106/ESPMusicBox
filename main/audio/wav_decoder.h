#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *file;
    uint32_t sample_rate;
    uint32_t data_remaining;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t audio_format;
} wav_decoder_t;

bool wav_decoder_open(wav_decoder_t *decoder, const char *path);
size_t wav_decoder_read(wav_decoder_t *decoder, int16_t *samples, size_t max_samples);
void wav_decoder_close(wav_decoder_t *decoder);
