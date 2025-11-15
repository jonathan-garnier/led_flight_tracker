#include "clock_screen.h"
#include "led_matrix.h"
#include <stdio.h>

void ClockScreen::onEnter()
{
    accumulator = 0;

    // Until WiFiManager exists, the clock cannot sync real time.
    // So we ALWAYS start in NO_WIFI.
    state = NO_WIFI;
}

void ClockScreen::update(float dt)
{
    accumulator += dt;

    // Fake clock updates every second so display isn't static
    if (accumulator >= 1.0f)
    {
        accumulator = 0;

        fakeMinutes++;
        if (fakeMinutes >= 60) {
            fakeMinutes = 0;
            fakeHours = (fakeHours + 1) % 24;
        }

        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", fakeHours, fakeMinutes);
    }
}

void ClockScreen::render(LEDMatrix& matrix)
{
    matrix.clear();

    // Until WiFiManager is implemented:
    matrix.drawText(0, 4, "Connect WiFi", 0x00FFFF);
    matrix.drawText(0, 14, "for clock", 0x00FFFF);

    // We ALSO show the fake ticking time at the bottom
    matrix.drawText(10, 26, timeStr, 0xFFFFFF);
}
