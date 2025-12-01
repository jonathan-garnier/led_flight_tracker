idf_component_register(
    SRCS "wifi_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        espressif__arduino-esp32
        esp_wifi
        log
        esp_event
        esp_netif
        nvs_flash
)
