#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t sine_test_request(uint32_t seconds, uint16_t bits_per_sample);
void sine_test_run_if_pending(void);
