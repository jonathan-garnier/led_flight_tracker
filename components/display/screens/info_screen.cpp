#include "info_screen.h"
#include "led_matrix.h"
#include <esp_system.h>
#include <stdio.h>
#include <Fonts/TomThumb.h>

void InfoScreen::render(LEDMatrix& matrix)
{
    auto* d = matrix.raw();

    d->fillScreen(0);

    // Use tiny 3x5 font
    d->setFont(&TomThumb);
    d->setTextSize(1);
    d->setTextWrap(false);

    // Colors
    uint16_t cyan  = matrix.color565(0, 255, 255);
    uint16_t white = matrix.color565(255, 255, 255);
    uint16_t green = matrix.color565(0, 255, 0);
    uint16_t red   = matrix.color565(255, 0, 0);

    // ---- HEADER ----
    d->setCursor(0, 6);
    d->setTextColor(cyan);
    d->print("INFO / STATUS");

    // ---- SYSTEM ----
    d->setCursor(0, 14);
    d->setTextColor(white);
    d->print("LED Matrix v1");

    // ---- HEAP ----
    char heapStr[32];
    snprintf(heapStr, sizeof(heapStr), "Heap:%ld", esp_get_free_heap_size());
    d->setCursor(0, 22);
    d->setTextColor(green);
    d->print(heapStr);

    // ---- WIFI ----
    d->setCursor(0, 30);
    d->setTextColor(red);
    d->print("WiFi: Not Conn");
}
