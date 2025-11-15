idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES espressif__arduino-esp32 ESP32-HUB75-MatrixPanel-DMA Adafruit-GFX-Library Adafruit_BusIO display input
)

# Ensure we compile as modern C++
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
