#include "bh1750.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_state.h"
#include "audio_service.h"
#include "nvs_config.h"

#define BH1750_ADDRESS  0x23
#define BH1750_SDA_GPIO 8
#define BH1750_SCL_GPIO 9
#define BH1750_POWER_ON 0x01
#define BH1750_RESET    0x07
#define BH1750_CONT_HR  0x10

static const char *TAG = "bh1750";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_sensor;
static bool s_ready;

static esp_err_t send_command(uint8_t command)
{
    return i2c_master_transmit(s_sensor, &command, 1, 100);
}

esp_err_t bh1750_init(void)
{
    s_ready = false;
    esp_err_t err = ESP_OK;
    if (s_bus == NULL) {
        const i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = BH1750_SDA_GPIO,
            .scl_io_num = BH1750_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        err = i2c_new_master_bus(&bus_config, &s_bus);
        if (err != ESP_OK) return err;
    }
    if (s_sensor == NULL) {
        const i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BH1750_ADDRESS,
            .scl_speed_hz = 100000,
        };
        err = i2c_master_bus_add_device(s_bus, &device_config, &s_sensor);
        if (err != ESP_OK) return err;
    }
    if ((err = send_command(BH1750_POWER_ON)) != ESP_OK) return err;
    if ((err = send_command(BH1750_RESET)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    if ((err = send_command(BH1750_CONT_HR)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(180));
    s_ready = true;
    ESP_LOGI(TAG, "sensor ready at 0x%02x on SDA=%d SCL=%d", BH1750_ADDRESS,
             BH1750_SDA_GPIO, BH1750_SCL_GPIO);
    return ESP_OK;
}

esp_err_t bh1750_read_lux(float *lux)
{
    if (lux == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    uint8_t raw[2];
    esp_err_t err = i2c_master_receive(s_sensor, raw, sizeof(raw), 100);
    if (err == ESP_OK) {
        uint16_t value = ((uint16_t)raw[0] << 8) | raw[1];
        *lux = (float)value / 1.2f;
    }
    return err;
}

void sensor_task(void *arg)
{
    (void)arg;
    bool latched = false;
    for (;;) {
        if (!s_ready) {
            esp_err_t err = bh1750_init();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "sensor not detected (%s); retrying in 5 seconds",
                         esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        float lux;
        if (bh1750_read_lux(&lux) == ESP_OK) {
            app_state_set_lux(lux);
            app_config_t config;
            nvs_config_get(&config);
            if (config.mode != APP_MODE_BIRTHDAY) {
                latched = false;
            } else {
                bool above = config.trigger_direction == TRIGGER_ABOVE;
                if (latched) {
                    double rearm_level = above ? (double)config.trigger_lux - config.dead_zone_lux
                                               : (double)config.trigger_lux + config.dead_zone_lux;
                    if (above ? lux <= rearm_level : lux >= rearm_level) latched = false;
                } else {
                    bool triggered = above ? lux > config.trigger_lux : lux < config.trigger_lux;
                    if (triggered && !audio_service_is_active()) {
                        ESP_LOGI(TAG, "lux %.1f %s threshold %.1f -> play",
                                 lux, above ? "above" : "below", config.trigger_lux);
                        if (audio_service_play_birthday(config.birthday_count) == ESP_OK) {
                            latched = true;
                        }
                    }
                }
            }
        } else {
            s_ready = false;
            ESP_LOGW(TAG, "sensor read failed; reconnecting");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
