idf_component_register(
    SRCS "microphone.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver espressif__arduino-esp32 display
)
