#pragma once

#include "esp_err.h"

esp_err_t bh1750_init(void);
esp_err_t bh1750_read_lux(float *lux);
void sensor_task(void *arg);
