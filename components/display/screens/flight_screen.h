#pragma once

#include "base_screen.h"

class FlightScreen : public BaseScreen {
public:
    void onEnter() override;
    void update(float dt) override;
    void render(LEDMatrix& matrix) override;

private:
    float scrollOffset = 0.0f;      // Vertical scroll offset for flight list
    float scrollSpeed = 10.0f;      // Pixels per second
    float updateTimer = 0.0f;       // Timer for updating display
    int currentFlightIndex = 0;     // Which flight we're showing (for cycling)
    bool showNoFlights = false;     // Whether to show "no flights" message

    enum State {
        NO_WIFI,
        NO_LOCATION,
        LOADING,
        READY
    } state = NO_WIFI;
};
