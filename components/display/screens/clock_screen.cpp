#include "clock_screen.h"
#include "led_matrix.h"
#include "wifi_manager.h"
#include "time_sync.h"
#include <Fonts/TomThumb.h>
#include <stdio.h>

void ClockScreen::onEnter()
{
    accumulator = 0;
    colonVisible = true;
    colorHue = 0.0f;

    // Check WiFi and time sync status
    WiFiManager& wm = WiFiManager::instance();
    if (wm.getState() == WiFiState::CONNECTED) {
        if (time_sync_ready()) {
            state = READY;
        } else {
            state = SYNCING;
        }
    } else {
        state = NO_WIFI;
    }
}

void ClockScreen::update(float dt)
{
    accumulator += dt;

    // Update rainbow color continuously (cycles every 5 seconds)
    colorHue += dt * 72.0f; // 360 degrees / 5 seconds
    if (colorHue >= 360.0f) {
        colorHue -= 360.0f;
    }

    // Update every 0.5 seconds
    if (accumulator >= 0.5f) {
        accumulator = 0;

        // Toggle colon visibility for blinking effect
        colonVisible = !colonVisible;

        WiFiManager& wm = WiFiManager::instance();

        // Check WiFi state
        if (wm.getState() != WiFiState::CONNECTED) {
            state = NO_WIFI;
            return;
        }

        // Check if time is synced
        if (!time_sync_ready()) {
            state = SYNCING;
            return;
        }

        // We're connected and synced
        state = READY;

        // Get current local time
        struct tm timeinfo;
        time_sync_get_time(&timeinfo);

        // Format as 12-hour time (no seconds) - split into hours and minutes
        int hour12 = timeinfo.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;

        snprintf(hourStr, sizeof(hourStr), "%d", hour12);
        snprintf(minStr, sizeof(minStr), "%02d", timeinfo.tm_min);

        // Determine AM/PM
        if (timeinfo.tm_hour < 12) {
            strcpy(ampmStr, "AM");
        } else {
            strcpy(ampmStr, "PM");
        }
    }
}

// Helper function to convert HSV to RGB
static void hsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    float c = v * s;
    float x = c * (1.0f - fabs(fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r1, g1, b1;
    if (h < 60) {
        r1 = c; g1 = x; b1 = 0;
    } else if (h < 120) {
        r1 = x; g1 = c; b1 = 0;
    } else if (h < 180) {
        r1 = 0; g1 = c; b1 = x;
    } else if (h < 240) {
        r1 = 0; g1 = x; b1 = c;
    } else if (h < 300) {
        r1 = x; g1 = 0; b1 = c;
    } else {
        r1 = c; g1 = 0; b1 = x;
    }

    r = (uint8_t)((r1 + m) * 255);
    g = (uint8_t)((g1 + m) * 255);
    b = (uint8_t)((b1 + m) * 255);
}

void ClockScreen::render(LEDMatrix& matrix)
{
    matrix.clear();

    auto* d = matrix.raw();
    d->setFont(&TomThumb);
    d->setTextSize(1);

    if (state == NO_WIFI) {
        d->setCursor(2, 10);
        d->setTextColor(matrix.color565(0, 255, 255));
        d->print("Connect WiFi");
        d->setCursor(8, 18);
        d->print("for clock");
    }
    else if (state == SYNCING) {
        d->setCursor(8, 12);
        d->setTextColor(matrix.color565(255, 255, 0));
        d->print("Syncing");
        d->setCursor(8, 20);
        d->print("time...");
    }
    else { // READY
        // Calculate rainbow color
        uint8_t r, g, b;
        hsvToRgb(colorHue, 1.0f, 1.0f, r, g, b);
        uint16_t rainbowColor = matrix.color565(r, g, b);

        // Use larger text size for time
        d->setTextSize(2);

        // Display hour (right-aligned to center)
        d->setCursor(10, 12);
        d->setTextColor(rainbowColor);
        d->print(hourStr);

        // Display blinking colon
        if (colonVisible) {
            d->setCursor(28, 12);
            d->setTextColor(rainbowColor);
            d->print(":");
        }

        // Display minutes
        d->setCursor(34, 12);
        d->setTextColor(rainbowColor);
        d->print(minStr);

        // AM/PM indicator (smaller, bottom right with dimmer rainbow)
        uint8_t dimR = r / 2;
        uint8_t dimG = g / 2;
        uint8_t dimB = b / 2;
        d->setTextSize(1);
        d->setCursor(50, 26);
        d->setTextColor(matrix.color565(dimR, dimG, dimB));
        d->print(ampmStr);
    }
}
