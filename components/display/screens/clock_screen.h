#pragma once

#include "base_screen.h"

class ClockScreen : public BaseScreen {
public:
    void onEnter() override;
    void update(float dt) override;
    void render(LEDMatrix& matrix) override;

private:
    char hourStr[4] = "12";      // Hour part (up to 2 digits + null)
    char minStr[4] = "00";       // Minute part (2 digits + null)
    char ampmStr[4] = "AM";      // AM/PM indicator (2 chars + null)
    float accumulator = 0;
    bool colonVisible = true;    // For blinking colon effect
    float colorHue = 0.0f;       // For rainbow color effect

    enum State {
        NO_WIFI,
        SYNCING,
        READY
    } state = NO_WIFI;
};
