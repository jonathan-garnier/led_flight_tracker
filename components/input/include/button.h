#pragma once

#include <stdint.h>
#include "driver/gpio.h"

class Button {
public:
    Button(gpio_num_t pin, bool activeLow = true);

    void begin();
    void update();

    bool wasPressed();  // returns true once per press
    bool isHeld();      // returns true if currently held down
    bool wasLongPressed(uint32_t durationMs = 5000);  // returns true once after holding for duration

private:
    gpio_num_t _pin;
    bool _activeLow;

    bool _lastState = false;
    bool _currentState = false;
    bool _pressed = false;
    bool _longPressed = false;
    bool _longPressTriggered = false;

    uint32_t _pressStartTime = 0;
    uint32_t _lastDebounceTime = 0;
    uint32_t _debounceDelay = 30; // ms
};
