#include "led_matrix.h"
#include "Arduino.h" // for print()
#include <stdio.h>

// -----------------------------------------------------
// Constructor
// -----------------------------------------------------
LEDMatrix::LEDMatrix(int width, int height, int chain)
    : _width(width), _height(height), _chain(chain),
      _cfg(width, height, chain)
{
    // Apply your known-good pinout
    _cfg.gpio.r1 = 2;
    _cfg.gpio.g1 = 15;
    _cfg.gpio.b1 = 4;

    _cfg.gpio.r2 = 5;
    _cfg.gpio.g2 = 6;
    _cfg.gpio.b2 = 7;

    _cfg.gpio.a  = 8;
    _cfg.gpio.b  = 9;
    _cfg.gpio.c  = 10;
    _cfg.gpio.d  = 11;

    _cfg.gpio.clk = 12;
    _cfg.gpio.lat = 13;
    _cfg.gpio.oe  = 14;

    // You may add .e if using >32px tall panels
}

// -----------------------------------------------------
// Initialize the HUB75 panel
// -----------------------------------------------------
void LEDMatrix::begin()
{
    _panel = new MatrixPanel_I2S_DMA(_cfg);
    _panel->begin();
    _panel->setBrightness8(60);   // default brightness

    printf("LEDMatrix: HUB75 DMA display started.\n");
}

// -----------------------------------------------------
void LEDMatrix::clear()
{
    if (_panel)
        _panel->fillScreen(0);
}

void LEDMatrix::show()
{
    // HUB75 DMA panels update continuously.
    // If double-buffering is enabled later, handle it here.
}

void LEDMatrix::setBrightness(uint8_t b)
{
    if (_panel)
        _panel->setBrightness8(b);
}

// -----------------------------------------------------
// Drawing
// -----------------------------------------------------
void LEDMatrix::drawPixel(int x, int y, uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = color & 0xFF;

    drawPixelRGB(x, y, r, g, b);
}

void LEDMatrix::drawPixelRGB(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (_panel)
        _panel->drawPixelRGB888(x, y, r, g, b);
}

// -----------------------------------------------------
// Text — uses Arduino print
// -----------------------------------------------------
void LEDMatrix::drawText(int x, int y, const char* text, uint32_t color)
{
    if (!_panel) return;

    uint16_t col = color565(
        (color >> 16) & 0xFF,
        (color >> 8)  & 0xFF,
        (color & 0xFF)
    );

    _panel->setCursor(x, y);
    _panel->setTextColor(col);
    _panel->print(text);
}

// -----------------------------------------------------
// Color helper
// -----------------------------------------------------
uint16_t LEDMatrix::color565(uint8_t r, uint8_t g, uint8_t b)
{
    return _panel ? _panel->color565(r, g, b) : 0;
}
