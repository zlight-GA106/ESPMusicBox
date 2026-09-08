#pragma once

#include "esp_err.h"

esp_err_t usb_serial_init(void);
void config_task(void *arg);
