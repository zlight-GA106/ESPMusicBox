#include "board_pins_config.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"

#define MUSIC_BOX_I2S_BCLK_GPIO 4
#define MUSIC_BOX_I2S_WS_GPIO   5
#define MUSIC_BOX_I2S_DOUT_GPIO 6

esp_err_t get_i2s_pins(int port, board_i2s_pin_t *i2s_config)
{
    if (port != 0 || i2s_config == NULL) return ESP_ERR_INVALID_ARG;
    i2s_config->mck_io_num = I2S_GPIO_UNUSED;
    i2s_config->bck_io_num = MUSIC_BOX_I2S_BCLK_GPIO;
    i2s_config->ws_io_num = MUSIC_BOX_I2S_WS_GPIO;
    i2s_config->data_out_num = MUSIC_BOX_I2S_DOUT_GPIO;
    i2s_config->data_in_num = I2S_GPIO_UNUSED;
    return ESP_OK;
}
