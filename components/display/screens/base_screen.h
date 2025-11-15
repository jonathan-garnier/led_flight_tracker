#pragma once
#include "led_matrix.h"

class BaseScreen {
public:
    virtual ~BaseScreen() = default;

    // Called once when entering this screen
    virtual void onEnter() {}

    // Called once when leaving this screen
    virtual void onExit() {}

    // Called each frame (for animations)
    virtual void update(float dt) = 0;

    // Called each frame to draw content
    virtual void render(LEDMatrix& matrix) = 0;
};
