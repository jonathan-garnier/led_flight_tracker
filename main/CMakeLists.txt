idf_component_register(
    SRCS
        "main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        display
        input
        wifi_manager
        sensors
        utils
        espressif__arduino-esp32
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        log
)

# Ensure we compile as modern C++
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
