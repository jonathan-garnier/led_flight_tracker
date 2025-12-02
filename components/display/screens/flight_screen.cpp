#include "flight_screen.h"
#include "led_matrix.h"
#include "wifi_manager.h"
#include "app_config.h"
#include "flight_api.h"
#include <Fonts/TomThumb.h>
#include <stdio.h>
#include <math.h>

void FlightScreen::onEnter()
{
    scrollOffset = 0.0f;
    updateTimer = 0.0f;
    currentFlightIndex = 0;

    // Determine initial state
    WiFiManager& wm = WiFiManager::instance();
    if (wm.getState() != WiFiState::CONNECTED) {
        state = NO_WIFI;
    } else if (!AppConfig::instance().hasLocation()) {
        state = NO_LOCATION;
    } else if (FlightAPI::instance().getFlightCount() == 0) {
        state = LOADING;
    } else {
        state = READY;
    }
}

void FlightScreen::update(float dt)
{
    updateTimer += dt;

    // Check state every 0.5 seconds
    if (updateTimer >= 0.5f) {
        updateTimer = 0.0f;

        WiFiManager& wm = WiFiManager::instance();

        if (wm.getState() != WiFiState::CONNECTED) {
            state = NO_WIFI;
            return;
        }

        if (!AppConfig::instance().hasLocation()) {
            state = NO_LOCATION;
            return;
        }

        size_t flightCount = FlightAPI::instance().getFlightCount();
        if (flightCount == 0) {
            state = LOADING;
            showNoFlights = true;  // After first check, show "no flights" instead of loading
        } else {
            state = READY;
            showNoFlights = false;
        }
    }

    // Cycle through flights when in READY state
    if (state == READY && updateTimer >= 0.25f) {
        // Auto-cycle to next flight every 3 seconds
        static float cycleTimer = 0.0f;
        cycleTimer += dt;
        if (cycleTimer >= 3.0f) {
            cycleTimer = 0.0f;
            currentFlightIndex++;
            if (currentFlightIndex >= (int)FlightAPI::instance().getFlightCount()) {
                currentFlightIndex = 0;
            }
        }
    }
}

void FlightScreen::render(LEDMatrix& matrix)
{
    matrix.clear();

    auto* d = matrix.raw();
    d->setFont(&TomThumb);
    d->setTextSize(1);

    if (state == NO_WIFI) {
        d->setCursor(2, 10);
        d->setTextColor(matrix.color565(0, 255, 255));
        d->print("Connect WiFi");
        d->setCursor(2, 18);
        d->print("for flights");
    }
    else if (state == NO_LOCATION) {
        d->setCursor(2, 10);
        d->setTextColor(matrix.color565(255, 165, 0));
        d->print("Configure");
        d->setCursor(2, 18);
        d->print("location");
    }
    else if (state == LOADING) {
        if (showNoFlights) {
            d->setCursor(8, 12);
            d->setTextColor(matrix.color565(128, 128, 128));
            d->print("No flights");
            d->setCursor(12, 20);
            d->print("nearby");
        } else {
            d->setCursor(2, 12);
            d->setTextColor(matrix.color565(255, 255, 0));
            d->print("Loading");
            d->setCursor(2, 20);
            d->print("flights...");
        }
    }
    else { // READY
        const auto& flights = FlightAPI::instance().getFlights();
        if (currentFlightIndex >= (int)flights.size()) {
            currentFlightIndex = 0;
        }

        if (flights.empty()) {
            return;
        }

        const Flight& flight = flights[currentFlightIndex];

        // Draw flight callsign (large, centered)
        d->setTextSize(2);
        int16_t x1, y1;
        uint16_t w, h;
        d->getTextBounds(flight.callsign, 0, 0, &x1, &y1, &w, &h);
        int xPos = (64 - w) / 2;
        d->setCursor(xPos, 8);
        d->setTextColor(matrix.color565(0, 255, 0));
        d->print(flight.callsign);

        // Draw flight info (smaller text)
        d->setTextSize(1);

        // Altitude in feet
        int altFeet = (int)(flight.altitude * 3.28084f);  // Convert meters to feet
        char altStr[16];
        snprintf(altStr, sizeof(altStr), "ALT: %dft", altFeet);
        d->setCursor(2, 20);
        d->setTextColor(matrix.color565(100, 150, 255));
        d->print(altStr);

        // Speed in knots
        int speedKnots = (int)(flight.velocity * 1.94384f);  // Convert m/s to knots
        char speedStr[16];
        snprintf(speedStr, sizeof(speedStr), "SPD: %dkt", speedKnots);
        d->setCursor(2, 28);
        d->setTextColor(matrix.color565(255, 200, 0));
        d->print(speedStr);

        // Flight count indicator (top right corner)
        char countStr[16];
        snprintf(countStr, sizeof(countStr), "%d/%zu", currentFlightIndex + 1, flights.size());
        d->setCursor(42, 2);
        d->setTextColor(matrix.color565(128, 128, 128));
        d->print(countStr);
    }
}
