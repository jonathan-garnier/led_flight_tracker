idf_component_register(
    SRCS "flight_api.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client json app_config wifi_manager esp-tls
)
