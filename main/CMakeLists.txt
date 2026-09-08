set(SOURCES
    "app_main.c"
    "app_state.c"
    "audio/audio_service.c"
    "audio/i2s_player.c"
    "audio/mp3_decoder.c"
    "audio/sine_test.c"
    "audio/wav_decoder.c"
    "config/nvs_config.c"
    "filesystem/littlefs.c"
    "sensor/bh1750.c"
    "usb_serial/usb_serial.c"
    "web_api/web_api.c"
    "wifi/wifi_manager.c")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS
        "include"
        "audio"
        "config"
        "filesystem"
        "sensor"
        "usb_serial"
        "web_api"
        "wifi"
    REQUIRES
        app_update
        audio_pipeline
        audio_stream
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_uart
        esp_event
        esp_http_client
        esp_http_server
        esp_netif
        esp_partition
        esp_system
        esp_wifi
        json
        littlefs
        mbedtls
        nvs_flash
        esp-libhelix-mp3
        esp_tinyusb)

littlefs_create_partition_image(littlefs "${CMAKE_SOURCE_DIR}/littlefs" FLASH_IN_PROJECT)
