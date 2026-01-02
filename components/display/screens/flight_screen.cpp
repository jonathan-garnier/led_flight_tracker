#include "flight_screen.h"
#include "led_matrix.h"
#include "wifi_manager.h"
#include "app_config.h"
#include "flight_api.h"
#include <Fonts/TomThumb.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

// Helper function to calculate centered X position for text
// TomThumb font at size 2: 5 pixels per character (adjusted for actual rendering)
static int calculateCenteredX(const char* text, int displayWidth) {
    int textLen = strlen(text);
    int charWidth = 6;  // TomThumb font width at size 2 (actual rendered width)
    int totalWidth = textLen * (charWidth + 1);
    int x = (displayWidth - totalWidth) / 2;
    return (x < 0) ? 0 : x;  // Clamp to minimum of 0
}

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

        // Check if we have airport codes
        bool hasAirports = (flight.departureAirport[0] != '\0' && flight.arrivalAirport[0] != '\0');

        if (hasAirports) {
            // Display format with airport codes: FROM → TO (large, at top)
            d->setTextSize(1);

            // Display departure airport
            d->setCursor(2, 8);
            d->setTextColor(matrix.color565(0, 255, 0));
            d->print(flight.departureAirport);

            // Display arrow
            d->setCursor(26, 8);
            d->setTextColor(matrix.color565(100, 200, 100));
            d->print("=>");

            // Display arrival airport
            d->setCursor(38, 8);
            d->setTextColor(matrix.color565(0, 255, 200));
            d->print(flight.arrivalAirport);

            // Display flight callsign (line 2) - centered
            d->setTextSize(2);
            int callsignX = calculateCenteredX(flight.callsign, 64);
            d->setCursor(callsignX, 11);
            d->setTextColor(matrix.color565(255, 255, 0));
            d->print(flight.callsign);
            d->setTextSize(1);

            // Altitude in feet (line 3)
            int altFeet = (int)(flight.altitude * 3.28084f);  // Convert meters to feet
            char altStr[16];
            snprintf(altStr, sizeof(altStr), "ALT:%dft", altFeet);
            d->setCursor(2, 24);
            d->setTextColor(matrix.color565(100, 150, 255));
            d->print(altStr);

            // Speed in knots (line 3 right side)
            int speedKnots = (int)(flight.velocity * 1.94384f);  // Convert m/s to knots
            char speedStr[16];
            snprintf(speedStr, sizeof(speedStr), "%dkt", speedKnots);
            d->setCursor(38, 24);
            d->setTextColor(matrix.color565(255, 200, 0));
            d->print(speedStr);

        } else {
            // Display format without airport codes (fallback)
            d->setTextSize(2);

            // Draw flight callsign (line 1) - centered
            int callsignX = calculateCenteredX(flight.callsign, 64);
            d->setCursor(callsignX, 17);
            d->setTextColor(matrix.color565(0, 255, 0));
            d->print(flight.callsign);

            d->setTextSize(1);
            // Country (line 2 left) + Flight count (line 2 right)
            d->setCursor(2, 24);
            d->setTextColor(matrix.color565(100, 200, 255));
            d->print(flight.country);

            char countStr[16];
            snprintf(countStr, sizeof(countStr), "%d/%zu", currentFlightIndex + 1, flights.size());
            d->setCursor(42, 24);
            d->setTextColor(matrix.color565(128, 128, 128));
            d->print(countStr);

            // Altitude in meters (line 3 left)
            int altMeters = (int)(flight.altitude);
            char altStr[16];
            snprintf(altStr, sizeof(altStr), "ALT:%dm", altMeters);
            d->setCursor(2, 31);
            d->setTextColor(matrix.color565(100, 150, 255));
            d->print(altStr);

            // Speed in km/h (line 3 right)
            int speedKmh = (int)(flight.velocity * 3.6f);  // Convert m/s to km/h
            char speedStr[16];
            snprintf(speedStr, sizeof(speedStr), "%dkm", speedKmh);
            d->setCursor(42, 31);
            d->setTextColor(matrix.color565(255, 200, 0));
            d->print(speedStr);
        }

        // Flight count at top right for airport mode
        if (hasAirports) {
            char countStr[16];
            snprintf(countStr, sizeof(countStr), "%d/%zu", currentFlightIndex + 1, flights.size());
            d->setCursor(42, 2);
            d->setTextColor(matrix.color565(128, 128, 128));
            d->print(countStr);
        }
    }
}
