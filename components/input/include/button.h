#pragma once

#include <stdint.h>
#include "driver/gpio.h"

class Button {
public:
    Button(gpio_num_t pin, bool activeLow = true);

    void begin();
    void update();
    
    bool wasPressed();  // returns true once per press

private:
    gpio_num_t _pin;
    bool _activeLow;

    bool _lastState = false;
    bool _currentState = false;
    bool _pressed = false;

    uint32_t _lastDebounceTime = 0;
    uint32_t _debounceDelay = 30; // ms
};
