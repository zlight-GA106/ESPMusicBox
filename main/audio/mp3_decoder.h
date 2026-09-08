#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef int (*mp3_input_read_fn)(void *context, uint8_t *buffer, size_t length);
typedef bool (*mp3_stop_fn)(void *context);

esp_err_t mp3_decode_stream(mp3_input_read_fn read_fn,
                            void *read_context,
                            mp3_stop_fn should_stop,
                            void *stop_context);
