#pragma once

#include <stdint.h>
#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

class LEDMatrix {
public:
    LEDMatrix(int width, int height, int chain = 1);

    void begin();
    void clear();
    void show(); // optional: may not be needed w/ DMA
    void setBrightness(uint8_t b);

    // Drawing primitives
    void drawPixel(int x, int y, uint32_t color);
    void drawPixelRGB(int x, int y, uint8_t r, uint8_t g, uint8_t b);
    void drawText(int x, int y, const char* text, uint32_t color);

    // Color helper
    uint16_t color565(uint8_t r, uint8_t g, uint8_t b);

    // Provide access to low-level display
    MatrixPanel_I2S_DMA* raw() { return _panel; }

    int width() const { return _width; }
    int height() const { return _height; }

private:
    int _width;
    int _height;
    int _chain;

    HUB75_I2S_CFG _cfg;
    MatrixPanel_I2S_DMA* _panel = nullptr;
};
