#pragma once

#include "base_screen.h"

class ClockScreen : public BaseScreen {
public:
    void onEnter() override;
    void update(float dt) override;
    void render(LEDMatrix& matrix) override;

private:
    char timeStr[6] = "00:00";  // HH:MM
    float accumulator = 0;

    enum State {
        NO_WIFI,
        SYNCING,
        READY
    } state = NO_WIFI;

    // Placeholder fake time until WiFi exists
    int fakeHours = 12;
    int fakeMinutes = 0;
};
