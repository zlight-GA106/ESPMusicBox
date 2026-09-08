#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t i2s_player_init(void);
esp_err_t i2s_player_configure(uint32_t sample_rate);
void i2s_player_set_volume(uint8_t volume);
esp_err_t i2s_player_write(const int16_t *samples, size_t sample_count, uint8_t channels);
esp_err_t i2s_player_silence(void);
void i2s_player_get_signal_diagnostics(bool *bclk_toggled, bool *ws_toggled,
                                       bool *dout_toggled);
